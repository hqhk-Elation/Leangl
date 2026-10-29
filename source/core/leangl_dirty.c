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

#include "leangl_dirty.h"
#include "leangl_internal.h"
#include "leangl_port.h"
#include <stddef.h>
#include <stdint.h>

static LEANGL_Rect_t s_rects[LEANGL_DIRTY_MAX];
static uint8_t s_count = 0;

static void swap_u16(uint16_t *a, uint16_t *b) {
    uint16_t t = *a;
    *a = *b;
    *b = t;
}

static uint8_t overlaps(const LEANGL_Rect_t *a, const LEANGL_Rect_t *b) {
    if (a->x2 < b->x1 || b->x2 < a->x1)
        return 0;
    if (a->y2 < b->y1 || b->y2 < a->y1)
        return 0;
    return 1;
}

static void merge_into(LEANGL_Rect_t *dst, const LEANGL_Rect_t *src) {
    if (src->x1 < dst->x1)
        dst->x1 = src->x1;
    if (src->y1 < dst->y1)
        dst->y1 = src->y1;
    if (src->x2 > dst->x2)
        dst->x2 = src->x2;
    if (src->y2 > dst->y2)
        dst->y2 = src->y2;
}

/* 合并所有相交的矩形，直到表中两两不相交。 */
static void merge_all(void) {
    uint8_t i = 0;

    while (i < s_count) {
        uint8_t j = (uint8_t)(i + 1);
        while (j < s_count) {
            if (overlaps(&s_rects[i], &s_rects[j])) {
                merge_into(&s_rects[i], &s_rects[j]);
                s_rects[j] = s_rects[s_count - 1];
                s_count--;
            } else {
                j++;
            }
        }
        i++;
    }
}

void LEANGL_Dirty_Clear(void) {
    s_count = 0;
}

void LEANGL_Dirty_Clear_Range(uint16_t y1, uint16_t y2) {
    uint8_t i = 0;

    if (y1 > y2)
        swap_u16(&y1, &y2);

    while (i < s_count) {
        LEANGL_Rect_t *r = &s_rects[i];

        /* 与区间在纵向上没有重叠：整块保留。 */
        if (r->y2 < y1 || y2 < r->y1) {
            i++;
            continue;
        }

        /* 完全落在区间内：整块删掉。 */
        if (r->y1 >= y1 && r->y2 <= y2) {
            s_rects[i] = s_rects[s_count - 1];
            s_count--;
            continue; /* 换过来的那块还没检查，i 不递增 */
        }

        /* 区间从中间穿过：拆成上下两块。 */
        if (r->y1 < y1 && r->y2 > y2) {
            if (s_count < LEANGL_DIRTY_MAX) {
                LEANGL_Rect_t bottom = *r;
                bottom.y1 = (uint16_t)(y2 + 1);
                r->y2 = (uint16_t)(y1 - 1);
                s_rects[s_count] = bottom;
                s_count++;
            }
            /* 表满时保留整块：多画一次，但不会漏画。 */
            i++;
            continue;
        }

        if (r->y1 < y1)
            r->y2 = (uint16_t)(y1 - 1); /* 只保留上半部分 */
        else
            r->y1 = (uint16_t)(y2 + 1); /* 只保留下半部分 */

        i++;
    }
}

void LEANGL_Dirty_Add(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    LEANGL_Rect_t r;
    uint8_t i;

    if (x1 > x2)
        swap_u16(&x1, &x2);
    if (y1 > y2)
        swap_u16(&y1, &y2);

    if (leangl_port.screen.width == 0 || leangl_port.screen.height == 0)
        return;
    if (x1 >= leangl_port.screen.width || y1 >= leangl_port.screen.height)
        return;
    if (x2 >= leangl_port.screen.width)
        x2 = (uint16_t)(leangl_port.screen.width - 1);
    if (y2 >= leangl_port.screen.height)
        y2 = (uint16_t)(leangl_port.screen.height - 1);

    r.x1 = x1;
    r.y1 = y1;
    r.x2 = x2;
    r.y2 = y2;

    for (i = 0; i < s_count; i++) {
        if (overlaps(&s_rects[i], &r)) {
            merge_into(&s_rects[i], &r);
            merge_all();
            return;
        }
    }

    if (s_count < LEANGL_DIRTY_MAX) {
        s_rects[s_count] = r;
        s_count++;
    } else {
        merge_into(&s_rects[LEANGL_DIRTY_MAX - 1], &r);
        merge_all();
    }
}

void LEANGL_Dirty_Add_Rect(const LEANGL_Rect_t *rect) {
    if (rect == NULL)
        return;
    LEANGL_Dirty_Add(rect->x1, rect->y1, rect->x2, rect->y2);
}

uint8_t LEANGL_Dirty_Count(void) {
    return s_count;
}

const LEANGL_Rect_t *LEANGL_Dirty_Get(uint8_t index) {
    if (index >= s_count)
        return 0;
    return &s_rects[index];
}

uint8_t LEANGL_Dirty_Intersects(uint16_t y1, uint16_t y2) {
    uint8_t i;

    if (y1 > y2)
        swap_u16(&y1, &y2);

    for (i = 0; i < s_count; i++) {
        if (!(s_rects[i].y2 < y1 || y2 < s_rects[i].y1))
            return 1;
    }
    return 0;
}

uint8_t LEANGL_Dirty_Is_Empty(void) {
    return (s_count == 0);
}
