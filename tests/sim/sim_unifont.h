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

#ifndef LEANGL_UNIFONT_H
#define LEANGL_UNIFONT_H

/**
 * @file    sim_unifont.h
 * @brief   从 GNU Unifont 的 .hex 文件加载字体（仅主机端使用）。
 *
 * Unifont 的 .hex 格式每行一条记录：
 *
 *     码点(十六进制) ':' 点阵数据(十六进制)
 *
 *   例如：
 *     0041:0000000018242442427E424242420000      <- 8x16，16 字节
 *     4E2D:01000100010001003FF8210821082108...   <- 16x16，32 字节
 *
 * 宽度由数据长度决定：
 *   - 16 字节（32 个十六进制字符）-> 8x16 半角；
 *   - 32 字节（64 个十六进制字符）-> 16x16 全角。
 *
 * 位序为每行从左到右、每字节 MSB 在前，与 LEANGL_Glyph_t 的约定完全一致，
 * 因此加载后不需要任何位翻转。
 *
 * 用法：
 *     if (LEANGL_Unifont_Load("tests/assets/unifont_all-18.0.01.hex")) {
 *         LEANGL_Set_Font(LEANGL_Unifont_Font());
 *     }
 *     ...
 *     LEANGL_Unifont_Unload();
 *
 * 加载会把整个文件读入内存（unifont_all-18.0.01.hex 约 8 MB），再建一张按码点排序的
 * 索引表供二分查找。主机上内存充裕，这样比反复 fseek 快得多。
 */

#include "leangl_font.h"

/**
 * @brief 加载 .hex 字库文件。
 *
 * 重复调用会先卸载已加载的数据。文件不存在、格式非法或内存不足时返回 0，
 * 此时不会破坏已有状态（先解析成功后才替换）。
 *
 * @param path .hex 文件路径。
 *
 * @return 1 成功；0 失败。
 */
int LEANGL_Unifont_Load(const char *path);

/** @brief 卸载字库并释放内存。未加载时调用是安全的。 */
void LEANGL_Unifont_Unload(void);

/**
 * @brief 取字库对应的字体对象。
 *
 * 未加载时返回 NULL。
 */
const LEANGL_Font_t *LEANGL_Unifont_Font(void);

/** @brief 已加载的字形数量；未加载时为 0。 */
unsigned long LEANGL_Unifont_Count(void);

#endif /* LEANGL_UNIFONT_H */
