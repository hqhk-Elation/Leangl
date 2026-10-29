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

#include "leangl_button.h"
#include "leangl_draw_char.h"
#include "leangl_draw_shape.h"
#include "leangl_font.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 按当前字体测量文本尺寸。
 *
 * 宽度由 glyph_cb 逐字提供，因此中英文混排也能得到正确结果。
 * 字体尚未注册时两者都为 0；绘制时会再调用一次做懒测量（见 button_draw），
 * 所以"先建控件、后设字体"的用法也能正常工作。
 */
static void button_measure(LEANGL_Button_t *button) {
    button->text_w = LEANGL_Font_Text_Width(button->text);
    button->text_h = LEANGL_Font_Height();
}

static void button_draw(LEANGL_Widget_t *w) {
    LEANGL_Button_t *button = (LEANGL_Button_t *)w;
    LEANGL_Rect_t area = w->area;
    int dx;
    int dy;
    uint16_t x;
    uint16_t y;

    LEANGL_Fill_Rect(&area, button->pressed ? button->pressed_bg : button->bg);

    if (button->text == NULL)
        return;

    /* 懒测量：字体可能在控件创建之后才注册，此时缓存的尺寸还是 0。
       每次绘制都检查一次，代价极小，却能省掉一套"字体变更通知"机制。 */
    if (button->text_h == 0)
        button_measure(button);

    if (button->text_h == 0)
        return;

    dx = ((int)w->area.x2 - (int)w->area.x1 + 1 - (int)button->text_w) / 2;
    dy = ((int)w->area.y2 - (int)w->area.y1 + 1 - (int)button->text_h) / 2;
    if (dx < 0)
        dx = 0;
    if (dy < 0)
        dy = 0;

    x = (uint16_t)((int)w->area.x1 + dx);
    y = (uint16_t)((int)w->area.y1 + dy);

    LEANGL_Set_Char_Color(button->text_color);

    /* 按 Unicode 码点遍历，与 LEANGL_Font_Text_Width() 的测量方式一致。 */
    {
        const char *p = button->text;

        while (*p != '\0') {
            LEANGL_CodePoint_t cp;
            LEANGL_Font_Size_t size;

            if (x > w->area.x2)
                break;

            cp = LEANGL_Utf8_Next(&p);
            if (cp == LEANGL_CODEPOINT_NONE)
                continue; /* 非法字节序列已由解码器跳过 */

            size = LEANGL_Draw_Char_Ex(x, y, cp);
            if (size.w == 0)
                break;

            x = (uint16_t)(x + size.w);
        }
    }
}

void LEANGL_Button_Init(LEANGL_Button_t *button, const LEANGL_Rect_t *area,
                        const char *text) {
    if (button == NULL)
        return;

    LEANGL_Widget_Init(&button->base, area, button_draw, NULL);
    button->text = NULL;
    button->bg = LEANGL_Color_Hex(0x303030);
    button->pressed_bg = LEANGL_Color_Hex(0x0060C0);
    button->text_color = LEANGL_COLOR_WHITE;
    button->pressed = 0;
    button->text_w = 0;
    button->text_h = 0;
    button->on_click = NULL;

    LEANGL_Button_Set_Text(button, text);
}

void LEANGL_Button_Set_Text(LEANGL_Button_t *button, const char *text) {
    if (button == NULL)
        return;

    button->text = text;
    button_measure(button);
    LEANGL_Widget_Invalidate(&button->base);
}

void LEANGL_Button_Set_Colors(LEANGL_Button_t *button, LEANGL_Color_t bg,
                              LEANGL_Color_t pressed_bg,
                              LEANGL_Color_t text_color) {
    if (button == NULL)
        return;

    button->bg = bg;
    button->pressed_bg = pressed_bg;
    button->text_color = text_color;
    LEANGL_Widget_Invalidate(&button->base);
}

void LEANGL_Button_On_Click(LEANGL_Button_t *button, LEANGL_Button_Cb callback) {
    if (button == NULL)
        return;

    button->on_click = callback;
}

void LEANGL_Button_Press(LEANGL_Button_t *button) {
    if (button == NULL || button->pressed != 0)
        return;

    button->pressed = 1;
    LEANGL_Widget_Invalidate(&button->base);
}

void LEANGL_Button_Release(LEANGL_Button_t *button) {
    if (button == NULL || button->pressed == 0)
        return;

    button->pressed = 0;
    LEANGL_Widget_Invalidate(&button->base);

    if (button->on_click != NULL)
        button->on_click(button);
}

void LEANGL_Button_Cancel(LEANGL_Button_t *button) {
    if (button == NULL || button->pressed == 0)
        return;

    button->pressed = 0;
    LEANGL_Widget_Invalidate(&button->base);
}

uint8_t LEANGL_Button_Is_Pressed(const LEANGL_Button_t *button) {
    if (button == NULL)
        return 0;

    return button->pressed;
}
