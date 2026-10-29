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

#ifndef LEANGL_SIM_CONFIG_H
#define LEANGL_SIM_CONFIG_H

/**
 * @file    sim_config.h
 * @brief   Win32 屏幕模拟器的运行配置（模拟 MCU 运行环境）。
 *
 * 配置用极简的 INI 文本描述，不需要任何第三方依赖：
 *
 *     [display]
 *     width = 240
 *     height = 320
 *     band_lines = 40
 *
 *     [timing]
 *     flash_latency_us = 600
 *     raster_latency_us = 120
 *     flush_latency_us = 1800
 *     busy_wait = 1
 *
 *     [font]
 *     path = ../assets/unifont_all-18.0.01.hex
 *
 *     [log]
 *     verbose = 0
 *
 * 语法规则：
 *   - '#' 或 ';' 开头的行为注释，行尾的 " ;" / " #" 之后也算注释；
 *   - 节名与键名大小写不敏感；
 *   - 无法识别的键会被忽略并计数（见 Sim_Config_t::ignored_keys），不报错；
 *   - 键缺失时保留 Sim_Config_Init() 给出的默认值。
 *
 * 相对路径（如 font.path）一律相对**配置文件所在目录**解析，因此从哪个
 * 工作目录启动模拟器都不影响结果。
 */

#include <stddef.h>
#include <stdint.h>

/** 路径缓冲长度。 */
#define SIM_CONFIG_PATH_MAX 512

/** 模拟器配置。 */
typedef struct {
    /* ---- [display] ---- */
    uint16_t width;      /**< 屏幕宽度（像素）。 */
    uint16_t height;     /**< 屏幕高度（像素）。 */
    uint16_t band_lines; /**< 每条像素带的行数；0 表示按高度自动取 1/8。 */

    /* ---- [timing]：全部以微秒计，用来模拟 MCU 上的真实耗时 ---- */
    uint32_t flash_latency_us;  /**< 每次从字库（外部 Flash）取字模的耗时。 */
    uint32_t raster_latency_us; /**< 字形光栅化的耗时（仅 GDI 回退路径的缓存未命中）。 */
    uint32_t flush_latency_us;  /**< 每块像素带传屏（SPI/DMA）的耗时。 */
    int busy_wait;              /**< 1：忙等（模拟裸机 CPU）；0：Sleep（让出 CPU）。 */

    /* ---- [log] ---- */
    int verbose; /**< 1：打印每帧耗时与逐块刷新日志。 */

    /* ---- [font] ---- */
    char font_path[SIM_CONFIG_PATH_MAX]; /**< 字库路径；font_path_set 为 0 时表示未配置。 */
    int font_path_set;

    /* ---- 解析结果（只读） ---- */
    char base_dir[SIM_CONFIG_PATH_MAX]; /**< 配置文件所在目录，带结尾分隔符。 */
    int ignored_keys;                   /**< 被忽略的键数量（拼写错误时可用来自查）。 */
} Sim_Config_t;

/**
 * @brief 填入默认配置。
 *
 * 默认：240x320、像素带自动、所有延迟为 0（主机速度）、Sleep 模式。
 */
void Sim_Config_Init(Sim_Config_t *cfg);

/**
 * @brief 从文件加载配置。
 *
 * 先调用 Sim_Config_Init()，再逐键覆盖；文件里的相对路径会相对该文件解析。
 * 文件无法打开时返回 0，此时 cfg 保持默认值（base_dir 为空）。
 *
 * @return 1 成功；0 无法打开或读取。
 */
int Sim_Config_Load(Sim_Config_t *cfg, const char *path);

/**
 * @brief 在若干常见位置查找默认配置文件。
 *
 * @return 找到的路径（内部静态缓冲）或 NULL。
 */
const char *Sim_Config_Find_Default(void);

/**
 * @brief 按配置文件的相对位置解析路径。
 *
 * 绝对路径原样复制；相对路径拼到 base_dir 之后。
 *
 * @param cfg      已加载的配置。
 * @param in       输入路径。
 * @param out      输出缓冲。
 * @param out_size 输出缓冲大小。
 */
void Sim_Config_Resolve(const Sim_Config_t *cfg, const char *in, char *out,
                        size_t out_size);

/**
 * @brief 打印当前生效的配置。
 *
 * @param cfg  配置。
 * @param utf8 非 0 用中文文案，0 用英文（控制台不支持 UTF-8 时）。
 */
void Sim_Config_Print(const Sim_Config_t *cfg, int utf8);

#endif /* LEANGL_SIM_CONFIG_H */
