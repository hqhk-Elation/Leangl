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

#include "leangl_draw_shape.h"
#include "leangl_buffer.h"
#include "leangl_colors.h"
#include "leangl_draw_pixel.h"
#include "leangl_flush.h"
#include "leangl_internal.h"
#include "leangl_port.h"
#include <stddef.h>
#include <stdint.h>

void LEANGL_Draw_Pixel(uint16_t x, uint16_t y, LEANGL_Color_t color) {
    uint16_t w = (uint16_t)leangl_port.screen.width;
    int row = (int)y - (int)leangl_flush.y0;

    if (w == 0 || leangl_flush.rows == 0 || x >= w)
        return;
    if (row < 0 || row >= (int)leangl_flush.rows)
        return;

    LEANGL_Pixel_Write(
        LEANGL_Get_Buf() + ((uint32_t)row * w + x) * LEANGL_COLOR_BYTES,
        color);
}

void LEANGL_Draw_HLine(uint16_t x, uint16_t y, uint16_t len, LEANGL_Color_t color) {
    uint16_t w = (uint16_t)leangl_port.screen.width;
    int row = (int)y - (int)leangl_flush.y0;
    uint32_t end;
    uint8_t *p;

    if (w == 0 || leangl_flush.rows == 0 || len == 0 || x >= w)
        return;
    if (row < 0 || row >= (int)leangl_flush.rows)
        return;

    end = (uint32_t)x + len;
    if (end > w)
        end = w;

    p = LEANGL_Get_Buf() +
        ((uint32_t)row * w + x) * LEANGL_COLOR_BYTES;
    for (; x < end; x++) {
        LEANGL_Pixel_Write(p, color);
        p += LEANGL_COLOR_BYTES;
    }
}

void LEANGL_Draw_VLine(uint16_t x, uint16_t y, uint16_t len, LEANGL_Color_t color) {
    uint16_t w = (uint16_t)leangl_port.screen.width;
    int row = (int)y - (int)leangl_flush.y0;
    int end = row + (int)len;
    uint32_t stride;
    uint8_t *p;

    if (w == 0 || leangl_flush.rows == 0 || len == 0 || x >= w)
        return;

    if (row < 0)
        row = 0;
    if (end > (int)leangl_flush.rows)
        end = (int)leangl_flush.rows;
    if (row >= end)
        return;

    stride = (uint32_t)w * LEANGL_COLOR_BYTES;
    p = LEANGL_Get_Buf() + (uint32_t)row * stride +
        (uint32_t)x * LEANGL_COLOR_BYTES;
    for (; row < end; row++) {
        LEANGL_Pixel_Write(p, color);
        p += stride;
    }
}

void LEANGL_Fill_Rect(const LEANGL_Rect_t *rect, LEANGL_Color_t color) {
    uint16_t w = (uint16_t)leangl_port.screen.width;
    uint16_t x1;
    uint16_t x2;
    int y1;
    int y2;
    uint32_t stride;
    uint8_t *base;
    int y;

    if (rect == NULL || w == 0 || leangl_flush.rows == 0)
        return;

    x1 = rect->x1;
    x2 = rect->x2;
    if (x1 > x2) {
        uint16_t t = x1;
        x1 = x2;
        x2 = t;
    }
    if (x1 >= w)
        return;
    if (x2 >= w)
        x2 = (uint16_t)(w - 1);

    y1 = (int)rect->y1 - (int)leangl_flush.y0;
    y2 = (int)rect->y2 - (int)leangl_flush.y0;
    if (y1 < 0)
        y1 = 0;
    if (y2 >= (int)leangl_flush.rows)
        y2 = (int)leangl_flush.rows - 1;
    if (y1 > y2)
        return;

    stride = (uint32_t)w * LEANGL_COLOR_BYTES;
    base = LEANGL_Get_Buf();
    if (base == NULL)
        return;

    /* 填充整个矩形都用同一个颜色，先把它的字节形态算好再逐像素拷贝。
       原来是每个像素都调用一次 LEANGL_Pixel_Write()，其中的移位与字节序
       分支虽然会被优化，但把结果提到循环外总是更省。 */
    {
        uint8_t px[LEANGL_COLOR_BYTES];

#if LEANGL_COLOR_FORMAT_RGB565
        LEANGL_RGB565_To_Bytes(px, (uint16_t)color);
#else
        px[0] = (uint8_t)(color >> 16);
        px[1] = (uint8_t)(color >> 8);
        px[2] = (uint8_t)(color);
#endif

        for (y = y1; y <= y2; y++) {
            uint8_t *p = base + (uint32_t)y * stride +
                         (uint32_t)x1 * LEANGL_COLOR_BYTES;
            uint32_t x;

            for (x = x1; x <= x2; x++) {
                uint8_t i;

                for (i = 0; i < LEANGL_COLOR_BYTES; i++)
                    p[i] = px[i];
                p += LEANGL_COLOR_BYTES;
            }
        }
    }
}
