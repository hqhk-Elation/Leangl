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

#ifndef LEANGL_DRAW_CHAR_H
#define LEANGL_DRAW_CHAR_H

#include "leangl_colors.h"
#include "leangl_font.h"
#include <stdint.h>

/**
 * @file    leangl_draw_char.h
 * @brief   文本绘制：把字模点阵逐个像素写进当前缓冲区。
 *
 * 字体本身由 leangl_font.h 的 LEANGL_Font_t 描述，需先用 LEANGL_Set_Font()
 * 注册。本模块只负责"拿到字模后怎么打点"，不做字库查找。
 */

/**
 * @brief 设置字符绘制使用的前景色。
 *
 * 这是模块级状态，会影响之后所有 LEANGL_Draw_Char() 调用；控件在绘制前都会
 * 自己设置一次，因此一般不需要应用层关心。
 *
 * @param color 颜色值，需为当前颜色格式下定义的颜色（见 leangl_colors.h）。
 */
void LEANGL_Set_Char_Color(LEANGL_Color_t color);

/**
 * @brief 在指定位置绘制一个字符。
 *
 * 坐标为屏幕绝对坐标，字符会按当前刷新块裁剪，因此应在控件的 draw_cb
 * 中调用。未注册字体或该字符不存在时什么也不做。
 *
 * @param x  字符左上角横坐标（像素）。
 * @param y  字符左上角纵坐标（像素）。
 * @param ch 要绘制的字符，**Unicode 码点**（不是 UTF-8 字节）。
 */
void LEANGL_Draw_Char(uint16_t x, uint16_t y, LEANGL_CodePoint_t ch);

/**
 * @brief 绘制一个字符，并返回该字符的字模尺寸。
 *
 * 与 LEANGL_Draw_Char() 相同，额外返回字模宽高，便于比例字体按实际宽度
 * 推进光标。
 *
 * @param ch Unicode 码点。
 *
 * @return 字模的宽高；未注册字体或无此字时宽高均为 0。
 */
LEANGL_Font_Size_t LEANGL_Draw_Char_Ex(uint16_t x, uint16_t y,
                                       LEANGL_CodePoint_t ch);

/**
 * @brief 查询一个字符的字模尺寸，不绘制。
 *
 * @param ch Unicode 码点。
 *
 * @return 字模的宽高；未注册字体或无此字时为 0。
 */
LEANGL_Font_Size_t LEANGL_Char_Size(LEANGL_CodePoint_t ch);

/**
 * @brief 单行文本的绘制参数。
 *
 * 绘制范围用 clip_x1 / clip_x2 显式给出，因此可以把文本整体左移（负的 x）
 * 来实现滚动，也不会画到自己区域之外。
 *
 * 注意：裁剪是**按字符**做的——只有完整落在 [clip_x1, clip_x2] 内的字符才会
 * 被绘制，被切掉一半的字直接不画。库没有像素级的裁剪能力，这样既简单又不会
 * 越界写到邻居控件上。
 */
typedef struct {
    int32_t x;        /**< 文本起始横坐标，可为负（用于左移出屏）。 */
    uint16_t y;       /**< 文本顶部纵坐标。 */
    uint16_t clip_x1; /**< 允许绘制的左边界（含）。 */
    uint16_t clip_x2; /**< 允许绘制的右边界（含）。 */
    uint8_t ellipsis; /**< 非 0：内容放不下时在末尾画 "..."。 */
} LEANGL_Text_Line_t;

/**
 * @brief 绘制一行文本，支持按矩形横向裁剪与末尾省略号。
 *
 * 只处理一行：遇到 '\n' 或字符串结尾即停止，'\r' 被忽略。
 * 字体未注册、区域非法或文本为空时什么也不画。
 *
 * @param text 以 '\0' 结尾的 UTF-8 文本。
 * @param opt  绘制参数。
 *
 * @return 本行文本的总前进宽度（像素，未裁剪）；超过 UINT16_MAX 时钳位。
 *         无此字的字符不计入宽度。
 */
uint16_t LEANGL_Draw_Text_Line(const char *text, const LEANGL_Text_Line_t *opt);

#endif
