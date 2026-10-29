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

#ifndef LEANGL_TYPES_H
#define LEANGL_TYPES_H

#include <stdint.h>

/**
 * @brief 矩形区域描述。
 *
 * 使用左上角与右下角两个点表示一块矩形区域。
 */
typedef struct
{
    uint16_t x1, y1;  /**< 左上角坐标 */
    uint16_t x2, y2;  /**< 右下角坐标 */
}LEANGL_Rect_t;

/**
 * @brief 布尔类型定义。
 */
typedef enum{
    LEANGL_FALSE = 0,  /**< 假 */
    LEANGL_TRUE = 1    /**< 真 */
}LEANGL_Bool_t;

#endif