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
 * @file    sim_config.c
 * @brief   模拟器配置文件的解析（极简 INI，无第三方依赖）。
 *
 * 之所以手写而不是引入 json/cjson：模拟器是给 MCU 项目做验证用的工具，
 * 不应该把依赖带进使用者的构建环境。这里只需要“分节 + 键值”两件事。
 *
 * 解析策略：先整体填默认值，再逐行覆盖。任何一行的键名或取值有问题都只
 * 影响该行（计入 ignored_keys），不会让整份配置失效——模拟器仍然能跑起来，
 * 用户也能从“被忽略 N 项”看出拼写错误。
 */

#include "sim_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* 字符串辅助                                                          */
/* ================================================================== */

/** @brief 安全复制（总是以 '\0' 结尾）。 */
static void copy_str(char *dst, size_t size, const char *src) {
    if (size == 0)
        return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, size, "%s", src);
}

/** @brief 去掉首尾空白，返回新的起点。 */
static char *trim(char *s) {
    char *end;

    while (*s != '\0' && isspace((unsigned char)*s))
        s++;
    if (*s == '\0')
        return s;

    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        *end-- = '\0';

    return s;
}

/** @brief 大小写不敏感的字符串比较。 */
static int ieq(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/**
 * @brief 截掉行尾注释。
 *
 * 只有前面是空白的 ';' / '#' 才算注释起点，这样值里可以出现这两个字符
 * （例如路径 D:/a;b/），不会被误截。
 */
static void strip_comment(char *s) {
    char *p;

    for (p = s; *p != '\0'; p++) {
        if ((*p == ';' || *p == '#') &&
            (p == s || isspace((unsigned char)p[-1]))) {
            *p = '\0';
            return;
        }
    }
}

/* ================================================================== */
/* 取值解析                                                            */
/* ================================================================== */

/** @brief 解析非负十进制整数，要求整串都被消费。 */
static int parse_u32(const char *s, uint32_t *out) {
    char *end = NULL;
    unsigned long v;

    if (s == NULL || *s == '\0')
        return 0;

    v = strtoul(s, &end, 10);
    if (end == s)
        return 0;

    while (end != NULL && isspace((unsigned char)*end))
        end++;
    if (end != NULL && *end != '\0')
        return 0;

    *out = (uint32_t)v;
    return 1;
}

/** @brief 解析布尔值：1/0、true/false、yes/no、on/off。 */
static int parse_bool(const char *s, int *out) {
    if (ieq(s, "1") || ieq(s, "true") || ieq(s, "yes") || ieq(s, "on")) {
        *out = 1;
        return 1;
    }
    if (ieq(s, "0") || ieq(s, "false") || ieq(s, "no") || ieq(s, "off")) {
        *out = 0;
        return 1;
    }
    return 0;
}

/** @brief 判断路径是否为绝对路径。 */
static int path_is_absolute(const char *p) {
    if (p == NULL || p[0] == '\0')
        return 0;
    if (p[0] == '/' || p[0] == '\\')
        return 1;
    if (isalpha((unsigned char)p[0]) && p[1] == ':')
        return 1;
    return 0;
}

/* ================================================================== */
/* 默认值与路径                                                        */
/* ================================================================== */

void Sim_Config_Init(Sim_Config_t *cfg) {
    if (cfg == NULL)
        return;

    memset(cfg, 0, sizeof *cfg);

    cfg->width = 240;
    cfg->height = 320;
    cfg->band_lines = 0; /* 0 = 自动 */

    cfg->flash_latency_us = 0;
    cfg->raster_latency_us = 0;
    cfg->flush_latency_us = 0;
    cfg->busy_wait = 0;

    cfg->verbose = 0;

    cfg->font_path_set = 0;
    cfg->ignored_keys = 0;
}

/** @brief 记录 path 的目录部分（带结尾分隔符）到 cfg->base_dir。 */
static void set_base_dir(Sim_Config_t *cfg, const char *path) {
    size_t n = strlen(path);
    size_t i = n;

    while (i > 0 && path[i - 1] != '/' && path[i - 1] != '\\')
        i--;

    if (i == 0) {
        cfg->base_dir[0] = '\0';
        return;
    }
    if (i >= sizeof cfg->base_dir)
        i = sizeof cfg->base_dir - 1;

    memcpy(cfg->base_dir, path, i);
    cfg->base_dir[i] = '\0';
}

void Sim_Config_Resolve(const Sim_Config_t *cfg, const char *in, char *out,
                        size_t out_size) {
    if (out == NULL || out_size == 0)
        return;

    if (in == NULL) {
        out[0] = '\0';
        return;
    }

    if (path_is_absolute(in) || cfg == NULL || cfg->base_dir[0] == '\0') {
        copy_str(out, out_size, in);
        return;
    }

    snprintf(out, out_size, "%s%s", cfg->base_dir, in);
}

/* ================================================================== */
/* 逐键应用                                                            */
/* ================================================================== */

/**
 * @brief 把一条 key=value 应用到配置。
 *
 * @return 1 表示该键被识别并（在取值合法时）应用；0 表示不认识或取值非法。
 */
static int apply_key(Sim_Config_t *cfg, const char *section, const char *key,
                     const char *value) {
    uint32_t u;

    /* ---------------- [display] ---------------- */
    if (ieq(section, "display")) {
        if (ieq(key, "width") || ieq(key, "height")) {
            if (!parse_u32(value, &u) || u < 16u || u > 4096u)
                return 0;
            if (ieq(key, "width"))
                cfg->width = (uint16_t)u;
            else
                cfg->height = (uint16_t)u;
            return 1;
        }
        if (ieq(key, "band_lines")) {
            if (!parse_u32(value, &u) || u > 4096u)
                return 0;
            cfg->band_lines = (uint16_t)u;
            return 1;
        }
        return 0;
    }

    /* ---------------- [timing] ---------------- */
    if (ieq(section, "timing")) {
        const uint32_t k_max_us = 1000000u; /* 1 秒，再长就不像模拟器了 */

        if (ieq(key, "flash_latency_us")) {
            if (!parse_u32(value, &u) || u > k_max_us)
                return 0;
            cfg->flash_latency_us = u;
            return 1;
        }
        if (ieq(key, "raster_latency_us")) {
            if (!parse_u32(value, &u) || u > k_max_us)
                return 0;
            cfg->raster_latency_us = u;
            return 1;
        }
        if (ieq(key, "flush_latency_us")) {
            if (!parse_u32(value, &u) || u > k_max_us)
                return 0;
            cfg->flush_latency_us = u;
            return 1;
        }
        if (ieq(key, "busy_wait"))
            return parse_bool(value, &cfg->busy_wait);
        return 0;
    }

    /* ---------------- [log] ---------------- */
    if (ieq(section, "log")) {
        if (ieq(key, "verbose"))
            return parse_bool(value, &cfg->verbose);
        return 0;
    }

    /* ---------------- [font] ---------------- */
    if (ieq(section, "font")) {
        if (ieq(key, "path")) {
            if (value == NULL || value[0] == '\0')
                return 0;
            /* 相对路径按配置文件所在目录解析，之后与工作目录无关。 */
            Sim_Config_Resolve(cfg, value, cfg->font_path,
                               sizeof cfg->font_path);
            cfg->font_path_set = 1;
            return 1;
        }
        return 0;
    }

    return 0;
}

/* ================================================================== */
/* 加载                                                                */
/* ================================================================== */

int Sim_Config_Load(Sim_Config_t *cfg, const char *path) {
    FILE *fp;
    char line[1024];
    char section[64];
    int lineno = 0;

    if (cfg == NULL || path == NULL)
        return 0;

    Sim_Config_Init(cfg);
    set_base_dir(cfg, path);

    fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;

    section[0] = '\0';

    while (fgets(line, (int)sizeof line, fp) != NULL) {
        char *s;
        char *eq;
        char *value;

        lineno++;

        /* 首行可能是 UTF-8 BOM（记事本保存的文件常见）。 */
        if (lineno == 1 && (unsigned char)line[0] == 0xEFu &&
            (unsigned char)line[1] == 0xBBu && (unsigned char)line[2] == 0xBFu)
            memmove(line, line + 3, strlen(line + 3) + 1u);

        strip_comment(line);
        s = trim(line);

        if (*s == '\0')
            continue;

        /* [section] */
        if (*s == '[') {
            char *close = strchr(s, ']');

            if (close == NULL) {
                cfg->ignored_keys++;
                continue;
            }
            *close = '\0';
            copy_str(section, sizeof section, trim(s + 1));
            continue;
        }

        eq = strchr(s, '=');
        if (eq == NULL) {
            cfg->ignored_keys++;
            continue;
        }

        *eq = '\0';
        value = trim(eq + 1);

        if (!apply_key(cfg, section, trim(s), value))
            cfg->ignored_keys++;
    }

    fclose(fp);
    return 1;
}

const char *Sim_Config_Find_Default(void) {
    static const char *candidates[] = {
        "sim_config.ini",                  /* cwd 就是配置目录 */
        "sim/sim_config.ini",              /* cwd = tests/ */
        "tests/sim/sim_config.ini",        /* cwd = 仓库根 */
        "../sim/sim_config.ini",           /* cwd = tests/build */
        "../../tests/sim/sim_config.ini",  /* cwd = build/host/bin */
        "../../../tests/sim/sim_config.ini",
    };
    static char found[SIM_CONFIG_PATH_MAX];
    unsigned int i;

    for (i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
        FILE *fp = fopen(candidates[i], "rb");

        if (fp != NULL) {
            fclose(fp);
            copy_str(found, sizeof found, candidates[i]);
            return found;
        }
    }

    return NULL;
}

/* ================================================================== */
/* 打印                                                                */
/* ================================================================== */

void Sim_Config_Print(const Sim_Config_t *cfg, int utf8) {
    if (cfg == NULL)
        return;

    printf(utf8 ? "配置：%ux%u，像素带 %u 行%s\n"
                : "Config: %ux%u, band %u lines%s\n",
           (unsigned)cfg->width, (unsigned)cfg->height,
           (unsigned)cfg->band_lines,
           cfg->band_lines == 0
               ? (utf8 ? "（自动）" : " (auto)")
               : "");

    printf(utf8 ? "  延迟：字库读取 %lu us，光栅化 %lu us，刷屏 %lu us，%s\n"
                : "  Delay: flash %lu us, raster %lu us, flush %lu us, %s\n",
           (unsigned long)cfg->flash_latency_us,
           (unsigned long)cfg->raster_latency_us,
           (unsigned long)cfg->flush_latency_us,
           cfg->busy_wait ? (utf8 ? "忙等" : "busy-wait")
                          : (utf8 ? "Sleep" : "sleep"));

    if (cfg->font_path_set)
        printf(utf8 ? "  字库：%s\n" : "  Font: %s\n", cfg->font_path);

    if (cfg->ignored_keys > 0)
        printf(utf8 ? "  注意：%d 个配置项无法识别，已忽略\n"
                    : "  Warning: %d config entries ignored\n",
               cfg->ignored_keys);
}
