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
 * @file    leangl_font.c
 * @brief   字体注册与取字模。
 *
 * 本模块把"取字模"从绘制流程里独立出来：
 *   - 统一持有当前字体（leangl_port.font）；
 *   - 按码点缓存字模，重复取模不再调用 glyph_cb（LEANGL_GLYPH_CACHE_SIZE）；
 *   - 统一做尺寸校验，把非法字库挡在绘制之前。
 *
 * 绘制层（leangl_draw_char.c）只关心"拿到 LEANGL_Glyph_t 后怎么打点"。
 */

#include "leangl_font.h"
#include "leangl_conf.h"
#include "leangl_internal.h"
#include <stddef.h>
#include <stdint.h>

/*
 * 字形缓存
 * --------
 * glyph_cb 是绘制路径上最贵的一步（外部字库往往在 SPI Flash 里），而同一
 * 字符在"测量宽度 → 分块绘制 → 滚动副本"过程中会被反复取模。这里按码点
 * 缓存最近用过的字模，命中时不再调用回调。
 *
 * 策略：线性查找 + 先进先出替换。条目数很小（默认 8），线性查找比哈希更
 * 简单、没有冲突处理，也不会因为哈希分布不佳而抖动。
 */

#if LEANGL_GLYPH_CACHE_SIZE > 255
#error "LEANGL_GLYPH_CACHE_SIZE 不能超过 255（索引用 uint8_t）"
#endif

#if LEANGL_GLYPH_CACHE_SIZE > 0

/* 条目状态：空 / 有字模 / 已知无此字（负缓存，避免反复查询不存在的字符）。 */
#define GLYPH_CACHE_EMPTY 0u
#define GLYPH_CACHE_VALID 1u
#define GLYPH_CACHE_MISSING 2u

typedef struct {
    LEANGL_CodePoint_t ch; /**< 码点，仅 state != EMPTY 时有效。 */
    uint8_t state;         /**< 见 GLYPH_CACHE_* 。 */
    LEANGL_Glyph_t glyph;  /**< 字模副本（MSB-first，与 LEANGL_Glyph_t 约定一致）。 */
} Glyph_Cache_Entry_t;

static Glyph_Cache_Entry_t s_cache[LEANGL_GLYPH_CACHE_SIZE];
static uint8_t s_cache_used = 0; /**< 已占用条目数（不含被替换的）。 */
static uint8_t s_cache_next = 0; /**< 缓存满时的替换游标（FIFO）。 */
static const LEANGL_Font_t *s_cache_font = NULL; /**< 缓存所属字体，换字体即失效。 */

static void glyph_cache_reset(void) {
    uint8_t i;

    for (i = 0; i < LEANGL_GLYPH_CACHE_SIZE; i++)
        s_cache[i].state = GLYPH_CACHE_EMPTY;
    s_cache_used = 0;
    s_cache_next = 0;
    s_cache_font = NULL;
}

#else /* 关闭缓存：保留原来的单实例字模缓冲。 */

static LEANGL_Glyph_t s_glyph;

#endif

/** @brief 清空字模尺寸（保留 bits 内容，下次取模会覆盖）。 */
static void glyph_invalidate(LEANGL_Glyph_t *glyph) {
    glyph->size.w = 0;
    glyph->size.h = 0;
}

/**
 * @brief 校验回调回填的字模尺寸，挡住越界字库。
 *
 * 绘制时按 ceil(w/8) 字节一行寻址，所以这里必须用同一个公式，否则非 8 倍数
 * 宽度（如 12px）会读错行。尺寸非法时把宽高清零，调用方据此判定“无此字”。
 */
static uint8_t glyph_validate(LEANGL_Glyph_t *glyph) {
    uint16_t bytes_per_row;
    uint8_t w = glyph->size.w;
    uint8_t h = glyph->size.h;

    if (w == 0 || h == 0 || w > LEANGL_MAX_FONT_W || h > LEANGL_MAX_FONT_H) {
        glyph_invalidate(glyph);
        return 0;
    }

    bytes_per_row = (uint16_t)((w + 7u) / 8u);
    if ((uint32_t)bytes_per_row * h > LEANGL_MAX_GLYPH_BYTES) {
        glyph_invalidate(glyph);
        return 0;
    }

    return 1;
}

#if LEANGL_GLYPH_CACHE_SIZE > 0

/**
 * @brief 在缓存中查一个字模，必要时调用 glyph_cb 填充。
 *
 * @return 命中/新取到的字模；已知无此字时返回 NULL。
 */
static const LEANGL_Glyph_t *glyph_cache_lookup(const LEANGL_Font_t *font,
                                                LEANGL_CodePoint_t ch) {
    Glyph_Cache_Entry_t *entry;
    uint8_t i;
    uint8_t idx;

    /* 字体换了：旧字模全部作废。 */
    if (s_cache_font != font) {
        glyph_cache_reset();
        s_cache_font = font;
    }

    for (i = 0; i < s_cache_used; i++) {
        if (s_cache[i].ch != ch)
            continue;

        return (s_cache[i].state == GLYPH_CACHE_VALID) ? &s_cache[i].glyph
                                                       : NULL;
    }

    /* 未命中：有空位就用，没有就替换最早进入的那条。 */
    if (s_cache_used < LEANGL_GLYPH_CACHE_SIZE) {
        idx = s_cache_used;
        s_cache_used = (uint8_t)(s_cache_used + 1u);
    } else {
        idx = s_cache_next;
        s_cache_next =
            (uint8_t)((s_cache_next + 1u) % LEANGL_GLYPH_CACHE_SIZE);
    }

    entry = &s_cache[idx];
    entry->ch = ch;
    glyph_invalidate(&entry->glyph);

    if (!font->glyph_cb(font, ch, &entry->glyph) ||
        !glyph_validate(&entry->glyph)) {
        entry->state = GLYPH_CACHE_MISSING;
        return NULL;
    }

    entry->state = GLYPH_CACHE_VALID;
    return &entry->glyph;
}

#endif /* LEANGL_GLYPH_CACHE_SIZE > 0 */

void LEANGL_Set_Font(const LEANGL_Font_t *font) {
    /* 字体对象必须带取模回调，否则等同于没注册。 */
    if (font != NULL && font->glyph_cb == NULL)
        font = NULL;

    leangl_port.font = font;

    /* 旧字模属于旧字体，不能再复用。 */
#if LEANGL_GLYPH_CACHE_SIZE > 0
    glyph_cache_reset();
#else
    glyph_invalidate(&s_glyph);
#endif
}

const LEANGL_Font_t *LEANGL_Get_Font(void) {
    return leangl_port.font;
}

uint8_t LEANGL_Font_Height(void) {
    return (leangl_port.font != NULL) ? leangl_port.font->h : 0;
}

LEANGL_CodePoint_t LEANGL_Utf8_Next(const char **pp) {
    const uint8_t *s;
    LEANGL_CodePoint_t cp;
    uint8_t lead;
    int extra;
    int i;

    if (pp == NULL || *pp == NULL)
        return LEANGL_CODEPOINT_NONE;

    s = (const uint8_t *)*pp;
    lead = s[0];

    /* 末尾：不移动指针，调用方靠返回 0 结束循环。 */
    if (lead == 0x00u)
        return LEANGL_CODEPOINT_NONE;

    /* ASCII：单字节直通。 */
    if (lead < 0x80u) {
        *pp += 1;
        return (LEANGL_CodePoint_t)lead;
    }

    /* 按首字节的高位模式判断后续字节数。 */
    if ((lead & 0xE0u) == 0xC0u) {
        cp = (LEANGL_CodePoint_t)(lead & 0x1Fu);
        extra = 1;
    } else if ((lead & 0xF0u) == 0xE0u) {
        cp = (LEANGL_CodePoint_t)(lead & 0x0Fu);
        extra = 2;
    } else if ((lead & 0xF8u) == 0xF0u) {
        cp = (LEANGL_CodePoint_t)(lead & 0x07u);
        extra = 3;
    } else {
        /* 0x80~0xBF（孤立续字节）或 0xF8~0xFF：非法首字节，跳过它。 */
        *pp += 1;
        return LEANGL_CODEPOINT_NONE;
    }

    /* 校验并合成续字节。任一续字节不合法就把整个序列丢掉，
       但指针仍前进到已知的合法边界，保证不会死循环。 */
    for (i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0u) != 0x80u) {
            *pp += i; /* 停在第一个非法字节上，下次会重新尝试 */
            return LEANGL_CODEPOINT_NONE;
        }
        cp = (cp << 6) | (LEANGL_CodePoint_t)(s[i] & 0x3Fu);
    }

    *pp += (size_t)extra + 1u;

    /* 过长编码（overlong）与代理区码点都是无效的，按非法处理。 */
    if (cp < 0x80u || cp > 0x10FFFFu ||
        (cp >= 0xD800u && cp <= 0xDFFFu))
        return LEANGL_CODEPOINT_NONE;

    return cp;
}

uint16_t LEANGL_Font_Text_Width(const char *text) {
    uint32_t total = 0;
    const char *p = text;

    if (text == NULL)
        return 0;

    /* 逐码点遍历：一个 UTF-8 中文字符只查一次字模。 */
    while (*p != '\0') {
        LEANGL_CodePoint_t cp = LEANGL_Utf8_Next(&p);
        const LEANGL_Glyph_t *glyph;

        if (cp == LEANGL_CODEPOINT_NONE)
            continue; /* 非法序列已由解码器跳过，继续看下一个字符 */

        glyph = LEANGL_Font_Glyph(cp);
        if (glyph != NULL)
            total += glyph->size.w;

        if (total > UINT16_MAX) /* 超长文本直接钳住，避免回绕 */
            return UINT16_MAX;
    }

    return (uint16_t)total;
}

uint8_t LEANGL_Font_Text_Height(const char *text) {
    uint8_t tallest = 0;
    const char *p = text;

    if (text == NULL)
        return 0;

    while (*p != '\0') {
        LEANGL_CodePoint_t cp = LEANGL_Utf8_Next(&p);
        const LEANGL_Glyph_t *glyph;

        if (cp == LEANGL_CODEPOINT_NONE)
            continue;

        glyph = LEANGL_Font_Glyph(cp);
        if (glyph != NULL && glyph->size.h > tallest)
            tallest = glyph->size.h;
    }

    return tallest;
}

const LEANGL_Glyph_t *LEANGL_Font_Glyph(LEANGL_CodePoint_t ch) {
    const LEANGL_Font_t *font = leangl_port.font;

    if (font == NULL || font->glyph_cb == NULL)
        return NULL;

#if LEANGL_GLYPH_CACHE_SIZE > 0
    /* 命中缓存时完全不调用 glyph_cb。 */
    return glyph_cache_lookup(font, ch);
#else
    glyph_invalidate(&s_glyph);

    /* 回调负责填 s_glyph；失败表示无此字。 */
    if (!font->glyph_cb(font, ch, &s_glyph) || !glyph_validate(&s_glyph))
        return NULL;

    return &s_glyph;
#endif
}
