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

#include "leangl_widget.h"
#include "leangl_dirty.h"
#include "leangl_internal.h"
#include <stddef.h>
#include <stdint.h>

/* 根节点不绘制，只作为顶层控件的父节点。 */
static LEANGL_Widget_t s_root = { .visible = 1 };

static uint8_t is_ancestor(const LEANGL_Widget_t *ancestor,
                           const LEANGL_Widget_t *w) {
    while (w != NULL) {
        if (w == ancestor)
            return 1;
        w = w->parent;
    }
    return 0;
}

static void detach_only(LEANGL_Widget_t *w) {
    LEANGL_Widget_t *parent = w->parent;
    LEANGL_Widget_t *prev = NULL;
    LEANGL_Widget_t *cur;

    if (parent == NULL)
        return;

    cur = parent->first_child;
    while (cur != NULL && cur != w) {
        prev = cur;
        cur = cur->next_sibling;
    }
    if (cur == NULL)
        return;

    if (prev != NULL)
        prev->next_sibling = w->next_sibling;
    else
        parent->first_child = w->next_sibling;

    if (parent->last_child == w)
        parent->last_child = prev;

    w->parent = NULL;
    w->next_sibling = NULL;
}

static void attach(LEANGL_Widget_t *parent, LEANGL_Widget_t *child) {
    if (parent == NULL || child == NULL || parent == child)
        return;
    if (is_ancestor(child, parent))
        return; /* 会把树接成环 */

    if (child->parent != NULL)
        detach_only(child);

    child->parent = parent;
    child->next_sibling = NULL;

    if (parent->last_child != NULL)
        parent->last_child->next_sibling = child;
    else
        parent->first_child = child;
    parent->last_child = child;

    LEANGL_Widget_Invalidate(child);
}

void LEANGL_Widget_Init(LEANGL_Widget_t *w, const LEANGL_Rect_t *area,
                        LEANGL_Widget_Draw_cb draw_cb,
                        LEANGL_Widget_Event_cb event_cb) {
    if (w == NULL)
        return;

    if (area != NULL) {
        w->area = *area;
    } else {
        w->area.x1 = 0;
        w->area.y1 = 0;
        w->area.x2 = 0;
        w->area.y2 = 0;
    }

    w->visible = 1;
    w->draw_cb = draw_cb;
    w->event_cb = event_cb;
    w->parent = NULL;
    w->first_child = NULL;
    w->last_child = NULL;
    w->next_sibling = NULL;
}

void LEANGL_Widget_Invalidate(LEANGL_Widget_t *w) {
    if (w == NULL)
        return;

    LEANGL_Dirty_Add_Rect(&w->area);
}

static void invalidate_subtree(LEANGL_Widget_t *w) {
    LEANGL_Widget_t *cur;

    LEANGL_Widget_Invalidate(w);

    for (cur = w->first_child; cur != NULL; cur = cur->next_sibling)
        invalidate_subtree(cur);
}

void LEANGL_Widget_Add(LEANGL_Widget_t *w) {
    attach(&s_root, w);
}

void LEANGL_Widget_Add_Child(LEANGL_Widget_t *parent, LEANGL_Widget_t *child) {
    attach(parent != NULL ? parent : &s_root, child);
}

void LEANGL_Widget_Remove(LEANGL_Widget_t *w) {
    if (w == NULL)
        return;

    detach_only(w);
    invalidate_subtree(w);
}

static void process_subtree(LEANGL_Widget_t *w) {
    LEANGL_Widget_t *cur;

    if (w->event_cb != NULL && w->event_cb(w))
        LEANGL_Widget_Invalidate(w);

    for (cur = w->first_child; cur != NULL; cur = cur->next_sibling)
        process_subtree(cur);
}

void LEANGL_Widget_Process(void) {
    LEANGL_Widget_t *cur;

    for (cur = s_root.first_child; cur != NULL; cur = cur->next_sibling)
        process_subtree(cur);
}

static void draw_subtree(LEANGL_Widget_t *w, uint16_t y0, uint16_t y1) {
    LEANGL_Widget_t *cur;

    if (!w->visible)
        return;

    if (w->draw_cb != NULL && !(w->area.y2 < y0 || y1 < w->area.y1))
        w->draw_cb(w);

    for (cur = w->first_child; cur != NULL; cur = cur->next_sibling)
        draw_subtree(cur, y0, y1);
}

void LEANGL_Widget_Draw(uint16_t y0, uint16_t y1) {
    LEANGL_Widget_t *cur;
    uint16_t saved_y0 = leangl_flush.y0;
    uint16_t saved_rows = leangl_flush.rows;
    uint32_t rows;

    if (y1 < y0)
        return;

    /* 绘制函数按“缓冲区内的行号”寻址，行数一旦超过一块缓冲区就会写越界。
       本函数是公开接口，调用方传什么都要先钳住。 */
    rows = (uint32_t)y1 - y0 + 1;
    if (rows > buffer.lines)
        rows = buffer.lines;
    if (rows == 0) /* 缓冲区还没初始化 */
        return;

    /* 进入本块前强制设置刷新窗口，避免调用方绕过 LEANGL_Flush() 时
       leangl_flush 仍为 0，导致所有块都按 y0 = 0 绘制、文字逐块重复。 */
    leangl_flush.y0 = y0;
    leangl_flush.rows = (uint16_t)rows;
    y1 = (uint16_t)((uint32_t)y0 + rows - 1);

    for (cur = s_root.first_child; cur != NULL; cur = cur->next_sibling)
        draw_subtree(cur, y0, y1);

    leangl_flush.y0 = saved_y0;
    leangl_flush.rows = saved_rows;
}
