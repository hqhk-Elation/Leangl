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
 * @file    test_sim_config.c
 * @brief   模拟器配置文件解析的单元测试。
 *
 * 覆盖点：
 *   1. 默认值合理；
 *   2. 完整配置（含 BOM、CRLF、行尾注释）能被正确读出；
 *   3. 缺省的键保留默认值；
 *   4. 越界取值与未知键会被忽略并计数，而不是让整份配置失效；
 *   5. 文件不存在时返回 0；
 *   6. font.path 之类的相对路径按配置文件所在目录解析。
 *
 * 这个测试不依赖 Windows API，任何主机都能跑。
 */

#include "sim_config.h"

#include <stdio.h>
#include <string.h>

static int s_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            s_failures++;                                                      \
        }                                                                      \
    } while (0)

#define CHECK_STR(a, b)                                                        \
    do {                                                                       \
        if (strcmp((a), (b)) != 0) {                                           \
            printf("FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a),  \
                   (b));                                                       \
            s_failures++;                                                      \
        }                                                                      \
    } while (0)

static const char *write_file(const char *path, const char *text) {
    FILE *fp = fopen(path, "wb");

    if (fp == NULL)
        return NULL;

    fwrite(text, 1, strlen(text), fp);
    fclose(fp);
    return path;
}

/* ================================================================== */

static void test_defaults(void) {
    Sim_Config_t cfg;

    Sim_Config_Init(&cfg);

    CHECK(cfg.width == 240);
    CHECK(cfg.height == 320);
    CHECK(cfg.band_lines == 0); /* 0 = 自动 */
    CHECK(cfg.flash_latency_us == 0);
    CHECK(cfg.raster_latency_us == 0);
    CHECK(cfg.flush_latency_us == 0);
    CHECK(cfg.busy_wait == 0);
    CHECK(cfg.verbose == 0);
    CHECK(cfg.font_path_set == 0);
    CHECK(cfg.ignored_keys == 0);
    CHECK(cfg.base_dir[0] == '\0');
}

static void test_full_parse(void) {
    static const char k_text[] =
        /* UTF-8 BOM 放在字符串开头（分段拼接避免 \x 转义吃字符） */
        "\xEF\xBB\xBF"
        "[display]\r\n"
        "width = 320\r\n"
        "height=240\r\n"
        "band_lines = 30\r\n"
        "\r\n"
        "; 整行注释\r\n"
        "# 另一种整行注释\r\n"
        "[timing]\r\n"
        "flash_latency_us = 300\r\n"
        "raster_latency_us = 150  ; 行尾注释\r\n"
        "flush_latency_us = 2400\r\n"
        "busy_wait = yes\r\n"
        "[font]\r\n"
        "path = ../assets/unifont_all-18.0.01.hex\r\n"
        "[log]\r\n"
        "verbose = true\r\n";

    Sim_Config_t cfg;

    CHECK(write_file("sim_cfg_full.ini", k_text) != NULL);
    CHECK(Sim_Config_Load(&cfg, "sim_cfg_full.ini") == 1);

    CHECK(cfg.width == 320);
    CHECK(cfg.height == 240);
    CHECK(cfg.band_lines == 30);
    CHECK(cfg.flash_latency_us == 300);
    CHECK(cfg.raster_latency_us == 150);
    CHECK(cfg.flush_latency_us == 2400);
    CHECK(cfg.busy_wait == 1);
    CHECK(cfg.verbose == 1);
    CHECK(cfg.font_path_set == 1);
    CHECK_STR(cfg.font_path, "../assets/unifont_all-18.0.01.hex");
    CHECK(cfg.ignored_keys == 0);

    remove("sim_cfg_full.ini");
}

static void test_partial_keeps_defaults(void) {
    Sim_Config_t cfg;

    CHECK(write_file("sim_cfg_partial.ini",
                     "[display]\n"
                     "width = 128\n") != NULL);
    CHECK(Sim_Config_Load(&cfg, "sim_cfg_partial.ini") == 1);

    CHECK(cfg.width == 128);
    CHECK(cfg.height == 320);      /* 未配置，保持默认 */
    CHECK(cfg.flush_latency_us == 0);
    CHECK(cfg.font_path_set == 0);

    remove("sim_cfg_partial.ini");
}

static void test_invalid_entries(void) {
    Sim_Config_t cfg;

    /* 5 个应被忽略的条目：尺寸越界、延迟越界、布尔值非法、未知键、未知节。 */
    CHECK(write_file("sim_cfg_bad.ini",
                     "[display]\n"
                     "width = 5\n"          /* < 16 */
                     "height = 99999\n"     /* > 4096 */
                     "[timing]\n"
                     "flash_latency_us = 99999999\n"
                     "busy_wait = maybe\n"
                     "no_such_key = 1\n"
                     "[nonsense]\n"
                     "foo = bar\n") != NULL);
    CHECK(Sim_Config_Load(&cfg, "sim_cfg_bad.ini") == 1);

    CHECK(cfg.width == 240);
    CHECK(cfg.height == 320);
    CHECK(cfg.flash_latency_us == 0);
    CHECK(cfg.busy_wait == 0);
    CHECK(cfg.ignored_keys == 6);

    remove("sim_cfg_bad.ini");
}

static void test_missing_file(void) {
    Sim_Config_t cfg;

    CHECK(Sim_Config_Load(&cfg, "sim_cfg_does_not_exist.ini") == 0);
    CHECK(cfg.width == 240);
    CHECK(cfg.height == 320);
}

static void test_path_resolve(void) {
    Sim_Config_t cfg;
    char out[SIM_CONFIG_PATH_MAX];

    Sim_Config_Init(&cfg);

    /* 无目录信息时，相对路径原样保留（相对当前工作目录）。 */
    Sim_Config_Resolve(&cfg, "a.hex", out, sizeof out);
    CHECK_STR(out, "a.hex");

    snprintf(cfg.base_dir, sizeof cfg.base_dir, "tests/sim/");
    Sim_Config_Resolve(&cfg, "a.hex", out, sizeof out);
    CHECK_STR(out, "tests/sim/a.hex");

    /* 绝对路径不被拼接。 */
    Sim_Config_Resolve(&cfg, "D:/fonts/a.hex", out, sizeof out);
    CHECK_STR(out, "D:/fonts/a.hex");
    Sim_Config_Resolve(&cfg, "/usr/share/a.hex", out, sizeof out);
    CHECK_STR(out, "/usr/share/a.hex");
}

/* ================================================================== */

int main(void) {
    test_defaults();
    test_full_parse();
    test_partial_keeps_defaults();
    test_invalid_entries();
    test_missing_file();
    test_path_resolve();

    if (s_failures == 0) {
        printf("sim_config: all checks passed\n");
        return 0;
    }

    printf("sim_config: %d check(s) failed\n", s_failures);
    return 1;
}
