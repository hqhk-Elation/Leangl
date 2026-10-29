/*
 * Copyright (C) 2026  黄楷翔
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file    unifont.c
 * @brief   GNU Unifont .hex 文件加载器。
 *
 * 实现方式：
 *   1. 一次性把整个文件读进内存；
 *   2. 逐行解析成 {码点, 数据偏移, 字节数} 的索引；
 *   3. 索引按码点排序，取字形时二分查找，再就地把十六进制解成字节。
 *
 * 选择"整文件入内存 + 索引"而不是"每次 fseek"，是因为字形查询非常频繁：
 * 一个字符在分块刷新时会被反复取模，走磁盘会明显拖慢模拟器。
 */

#include "sim_unifont.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 单条索引项。offset 指向 s_buf 中该字形十六进制数据的首字符。 */
typedef struct {
    uint32_t cp;     /**< Unicode 码点。 */
    uint32_t offset; /**< 十六进制数据在缓冲区中的偏移。 */
    uint8_t bytes;   /**< 点阵字节数：16（8x16）或 32（16x16）。 */
} Unifont_Entry_t;

static char *s_buf = NULL;             /* 整个 .hex 文件内容 */
static Unifont_Entry_t *s_index = NULL;
static uint32_t s_count = 0;

/* ------------------------------------------------------------------ */
/* 十六进制辅助                                                        */
/* ------------------------------------------------------------------ */

/** @brief 单个十六进制字符转数值；非法字符返回 -1。 */
static int hex_val(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/** @brief 把 s 开头的字节数 n 的十六进制串解码到 out。成功返回 1。 */
static int hex_decode(const char *s, uint32_t n, uint8_t *out) {
    uint32_t i;

    for (i = 0; i < n; i++) {
        int hi = hex_val(s[i * 2]);
        int lo = hex_val(s[i * 2 + 1]);

        if (hi < 0 || lo < 0)
            return 0;

        out[i] = (uint8_t)((hi << 4) | lo);
    }

    return 1;
}

/* ------------------------------------------------------------------ */
/* 索引排序与查找                                                      */
/* ------------------------------------------------------------------ */

static int entry_cmp(const void *a, const void *b) {
    const Unifont_Entry_t *ea = (const Unifont_Entry_t *)a;
    const Unifont_Entry_t *eb = (const Unifont_Entry_t *)b;

    if (ea->cp < eb->cp)
        return -1;
    if (ea->cp > eb->cp)
        return 1;
    return 0;
}

static const Unifont_Entry_t *entry_find(LEANGL_CodePoint_t cp) {
    uint32_t lo = 0;
    uint32_t hi = s_count;

    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;

        if (s_index[mid].cp == cp)
            return &s_index[mid];
        if (s_index[mid].cp < cp)
            lo = mid + 1;
        else
            hi = mid;
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/* 取字模回调                                                          */
/* ------------------------------------------------------------------ */

static uint8_t unifont_glyph(const LEANGL_Font_t *font, LEANGL_CodePoint_t ch,
                             LEANGL_Glyph_t *out) {
    const Unifont_Entry_t *e;

    (void)font;

    if (out == NULL)
        return 0;

    e = entry_find(ch);
    if (e == NULL)
        return 0; /* 字库里没有这个字符 */

    if (!hex_decode(s_buf + e->offset, e->bytes, out->bits))
        return 0;

    /* 16 字节 -> 8x16（半角）；32 字节 -> 16x16（全角）。 */
    out->size.w = (e->bytes == 16u) ? 8u : 16u;
    out->size.h = 16u;

    return 1;
}

/* 字体描述：高度固定 16，宽度由每个字形自己决定。
   定义在 unifont_glyph 之后，才能直接写函数地址。 */
static const LEANGL_Font_t s_unifont = LEANGL_FONT_INIT(16, unifont_glyph, NULL);

/* ------------------------------------------------------------------ */
/* 加载 / 卸载                                                         */
/* ------------------------------------------------------------------ */

static void unifont_free(void) {
    free(s_buf);
    free(s_index);
    s_buf = NULL;
    s_index = NULL;
    s_count = 0;
}

int LEANGL_Unifont_Load(const char *path) {
    FILE *fp;
    long size;
    char *buf = NULL;
    Unifont_Entry_t *idx = NULL;
    uint32_t cap = 0;
    uint32_t count = 0;
    char *line;

    if (path == NULL)
        return 0;

    /* ---- 1. 读入整个文件 ---- */
    fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    size = ftell(fp);
    if (size <= 0) {
        fclose(fp);
        return 0;
    }
    rewind(fp);

    /* 多留 1 字节放结尾 '\0'，方便按 C 字符串处理。 */
    buf = (char *)malloc((size_t)size + 1u);
    if (buf == NULL) {
        fclose(fp);
        return 0;
    }

    if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        free(buf);
        fclose(fp);
        return 0;
    }
    fclose(fp);
    buf[size] = '\0';

    /* ---- 2. 统计行数，为索引分配空间 ---- */
    {
        long i;
        for (i = 0; i < size; i++) {
            if (buf[i] == '\n')
                count++;
        }
        if (size > 0 && buf[size - 1] != '\n')
            count++; /* 最后一行没有换行符 */
    }

    if (count == 0) {
        free(buf);
        return 0;
    }

    idx = (Unifont_Entry_t *)malloc((size_t)count * sizeof(Unifont_Entry_t));
    if (idx == NULL) {
        free(buf);
        return 0;
    }

    /* ---- 3. 逐行解析 ---- */
    cap = 0;
    line = buf;
    while (*line != '\0' && cap < (uint32_t)count) {
        char *colon;
        char *end;
        unsigned long cp;
        uint32_t data_len;

        /* 行尾：找到 '\n' 并就地截断，让后续按 C 字符串处理。 */
        end = strchr(line, '\n');
        if (end != NULL)
            *end = '\0';

        /* 处理 CRLF：去掉行尾的 '\r'。 */
        if (end != line && end[-1] == '\r')
            end[-1] = '\0';

        colon = strchr(line, ':');
        if (colon != NULL && colon != line) {
            cp = strtoul(line, NULL, 16);

            /* 数据部分长度必须是 32 或 64 个十六进制字符（16 或 32 字节）。 */
            data_len = (uint32_t)strlen(colon + 1);

            if ((data_len == 32u || data_len == 64u) && cp <= 0x10FFFFul) {
                idx[cap].cp = (uint32_t)cp;
                idx[cap].offset =
                    (uint32_t)((colon + 1) - buf);
                idx[cap].bytes = (uint8_t)(data_len / 2u);
                cap++;
            }
        }

        if (end == NULL)
            break; /* 最后一行，后面没有更多数据 */
        line = end + 1;
    }

    if (cap == 0) {
        free(buf);
        free(idx);
        return 0;
    }

    /* ---- 4. 按码点排序，之后即可二分查找 ---- */
    qsort(idx, cap, sizeof(Unifont_Entry_t), entry_cmp);

    /* ---- 5. 全部成功才替换旧数据 ---- */
    unifont_free();
    s_buf = buf;
    s_index = idx;
    s_count = cap;

    return 1;
}

void LEANGL_Unifont_Unload(void) {
    unifont_free();
}

const LEANGL_Font_t *LEANGL_Unifont_Font(void) {
    if (s_count == 0)
        return NULL;

    return &s_unifont;
}

unsigned long LEANGL_Unifont_Count(void) {
    return (unsigned long)s_count;
}
