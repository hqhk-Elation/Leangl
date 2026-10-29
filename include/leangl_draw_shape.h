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

#ifndef LEANGL_DRAW_SHAPE_H
#define LEANGL_DRAW_SHAPE_H

#include "leangl_colors.h"
#include "leangl_types.h"
#include <stdint.h>

/**
 * @file    leangl_draw_shape.h
 * @brief   基础图形绘制：像素、水平/垂直线与矩形填充。
 *
 * 所有坐标均为屏幕绝对坐标，原点在屏幕左上角。绘制函数会按刷新模块内部
 * 维护的当前块窗口裁剪，超出该范围的部分直接丢弃；因此应在控件的 draw_cb
 * 中调用。
 */

/**
 * @brief 在指定位置绘制一个像素点。
 *
 * 像素写入当前活动缓冲区，超出屏幕范围的点会被忽略，不会造成越界访问。
 *
 * @param x     像素横坐标（像素）。
 * @param y     像素纵坐标（像素）。
 * @param color 颜色值，需为当前颜色格式下定义的颜色（见 leangl_colors.h）。
 */
void LEANGL_Draw_Pixel(uint16_t x, uint16_t y, LEANGL_Color_t color);

/**
 * @brief 绘制一条水平线。
 *
 * @param x     起始点横坐标（像素）。
 * @param y     纵坐标（像素）。
 * @param len   线条长度（像素）。
 * @param color 颜色值，需为当前颜色格式下定义的颜色（见 leangl_colors.h）。
 */
void LEANGL_Draw_HLine(uint16_t x, uint16_t y, uint16_t len, LEANGL_Color_t color);

/**
 * @brief 绘制一条垂直线。
 *
 * @param x     横坐标（像素）。
 * @param y     起始点纵坐标（像素）。
 * @param len   线条长度（像素）。
 * @param color 颜色值，需为当前颜色格式下定义的颜色（见 leangl_colors.h）。
 */
void LEANGL_Draw_VLine(uint16_t x, uint16_t y, uint16_t len, LEANGL_Color_t color);

/**
 * @brief 填充一块矩形区域。
 *
 * @param rect  需要填充的矩形区域，坐标为闭区间。
 * @param color 颜色值，需为当前颜色格式下定义的颜色（见 leangl_colors.h）。
 */
void LEANGL_Fill_Rect(const LEANGL_Rect_t *rect, LEANGL_Color_t color);

#endif
