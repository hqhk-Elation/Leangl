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
 * @file    leangl_list.c
 * @brief   列表控件：每项一行、选中项异色、超宽自动省略、选中项自动滚动到可见。
 *
 * 绘制全部委托给 LEANGL_Draw_Text_Line()，本文件只负责：
 *   - 决定每行画在哪（行高 × 行号）；
 *   - 决定用哪个颜色（选中 / 未选中）；
 *   - 维护滚动位置 top，使选中项始终在可见行内。
 *
 * 性能约定：
 *   - top（滚动位置）属于"状态"，只在选中项 / 行高 / 条目 / 区域变化时更新；
 *     绘制路径 list_draw() 只读 top，不再每帧重算几何量，避免每帧重复除法。
 *   - 可见行数 rows 在需要时算一次，并通过参数在内部函数间传递。
 */

#include "leangl_list.h"
#include "leangl_colors.h"
#include "leangl_draw_char.h"
#include "leangl_draw_shape.h"
#include "leangl_font.h"
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* 几何辅助                                                            */
/* ------------------------------------------------------------------ */

/** @brief 实际生效的行高：显式设置优先，否则用字体标称高度。 */
static uint16_t list_line_h(const LEANGL_List_t *list) {
    if (list->line_h != 0)
        return list->line_h;
    return LEANGL_Font_Height();
}

/** @brief 区域内能完整显示的行数。参数校验完整，可安全对外复用。 */
static uint16_t list_visible_rows(const LEANGL_List_t *list) {
    uint16_t line_h = list_line_h(list);
    uint32_t area_h;

    if (line_h == 0)
        return 0;
    if (list->base.area.y2 < list->base.area.y1)
        return 0;

    area_h = (uint32_t)list->base.area.y2 - list->base.area.y1 + 1u;
    return (uint16_t)(area_h / line_h);
}

/**
 * @brief 调整滚动位置，让选中项落在可见范围内。
 *
 * 只在需要时才移动 top，避免选中项滚动时整列表跳动。
 * rows 由调用方传入（调用方通常已算过），避免重复除法。
 *
 * 注意：本函数只修改状态（list->top），不触发重绘；
 * 调用方负责在状态变化后 invalidate。
 */
static void list_scroll_to_selection(LEANGL_List_t *list, uint16_t rows) {
    uint16_t sel = list->selected;

    if (rows == 0 || list->count == 0 || sel == LEANGL_LIST_NONE)
        return;

    if (sel < list->top) {
        list->top = sel;                             /* 选中项在上方，向上滚 */
    } else if (sel >= (uint32_t)list->top + rows) {
        list->top = (uint16_t)(sel - rows + 1u);      /* 在下方，向下滚 */
    }

    /* 条目不足以填满时从 0 开始，避免下方留白。 */
    if ((uint32_t)list->top + rows > list->count)
        list->top = (list->count > rows) ? (uint16_t)(list->count - rows) : 0u;
}

/**
 * @brief 选中项收敛：条目数变化后 selected 可能越界，这里一次性修正。
 *        从绘制路径剥离，只在 Set_Items 等状态变更处调用。
 */
static void list_clamp_selection(LEANGL_List_t *list) {
    if (list->count == 0) {
        list->selected = LEANGL_LIST_NONE;
        list->top = 0;
        return;
    }
    if (list->selected == LEANGL_LIST_NONE || list->selected >= list->count)
        list->selected = 0;
}

/* ------------------------------------------------------------------ */
/* 绘制                                                                */
/* ------------------------------------------------------------------ */

static void list_draw(LEANGL_Widget_t *w) {
    LEANGL_List_t *list = (LEANGL_List_t *)w;
    LEANGL_Rect_t area = w->area;
    uint16_t line_h;
    uint16_t rows;
    uint16_t r;
    uint16_t y;

    if (area.x2 < area.x1 || area.y2 < area.y1)
        return;

    /* 不透明时整块铺底：同时清掉上一次绘制留下的内容。 */
    if (!list->is_transparent)
        LEANGL_Fill_Rect(&area, list->bg_color);

    if (list->items == NULL || list->count == 0)
        return;

    line_h = list_line_h(list);
    rows = list_visible_rows(list);
    if (line_h == 0 || rows == 0)
        return;

    /* 绘制路径只读 top —— top 已在 Set_Selected / Set_Line_Height /
       Set_Items / 区域变化时维护好。若外部直接改了 area 未通知，
       这里做一次轻量防御（不改 top，只保证不会越界画空）。 */
    if ((uint32_t)list->top >= list->count)
        return;

    y = area.y1;

    for (r = 0; r < rows; r++) {
        uint32_t index = (uint32_t)list->top + r;
        uint16_t x1;
        LEANGL_Text_Line_t opt;
        LEANGL_Color_t color;
        uint8_t is_sel;

        if (index >= list->count)
            break; /* 条目已画完 */

        /* 整行在垂直方向超出区域：结束（闭区间，末行是 y + line_h - 1）。
           y 用累加维护，避免每轮乘法。 */
        if ((uint32_t)y + line_h - 1u > area.y2)
            break;

        /* 单独用布尔量判断，而不是比较颜色值：text_color 与 sel_color
           相同时（用户故意设成一样）也要能正确识别选中行。 */
        is_sel = (uint8_t)(list->selected != LEANGL_LIST_NONE &&
                           index == list->selected);
        color = is_sel ? list->sel_color : list->text_color;

        /* 选中行的背景条：只覆盖这一行，且限制在控件宽度内。 */
        if (is_sel && list->show_sel_bg) {
            LEANGL_Rect_t bar;

            bar.x1 = area.x1;
            bar.x2 = area.x2;
            bar.y1 = y;
            bar.y2 = (uint16_t)(y + line_h - 1u);
            LEANGL_Fill_Rect(&bar, list->sel_bg_color);
        }

        LEANGL_Set_Char_Color(color);

        /* 左边界留白，但不能把文本推到区域外。 */
        x1 = (uint16_t)(area.x1 + list->pad_x);
        if (x1 > area.x2)
            x1 = area.x1;

        opt.x = (int32_t)x1;
        opt.y = y;
        opt.clip_x1 = area.x1;
        opt.clip_x2 = area.x2;
        opt.ellipsis = list->ellipsis;

        (void)LEANGL_Draw_Text_Line(list->items[index], &opt);

        y = (uint16_t)(y + line_h);
    }
}

/* ------------------------------------------------------------------ */
/* 初始化与配置                                                        */
/* ------------------------------------------------------------------ */

void LEANGL_List_Init(LEANGL_List_t *list, const LEANGL_Rect_t *area,
                      const char *const *items, uint16_t count) {
    if (list == NULL)
        return;

    LEANGL_Widget_Init(&list->base, area, list_draw, NULL);

    list->items = items;
    list->count = (items != NULL) ? count : 0u;
    list->selected = (list->count > 0) ? 0u : LEANGL_LIST_NONE;
    list->top = 0;
    list->line_h = 0;   /* 用字体高度 */
    list->pad_x = 2;

    list->text_color = LEANGL_COLOR_WHITE;
    list->sel_color = LEANGL_COLOR_CYAN;
    list->bg_color = LEANGL_Color_Hex(0x181C24);
    list->sel_bg_color = LEANGL_Color_Hex(0x2A3550);

    list->is_transparent = 0;
    list->show_sel_bg = 0;
    list->ellipsis = 1;
}

void LEANGL_List_Set_Items(LEANGL_List_t *list, const char *const *items,
                           uint16_t count) {
    if (list == NULL)
        return;

    list->items = items;
    list->count = (items != NULL) ? count : 0u;
    list->top = 0;

    /* 条目变化后一次性收敛选中项，并重算滚动位置。 */
    list_clamp_selection(list);
    if (list->count > 0)
        list_scroll_to_selection(list, list_visible_rows(list));

    LEANGL_Widget_Invalidate(&list->base);
}

void LEANGL_List_Set_Selected(LEANGL_List_t *list, uint16_t index) {
    if (list == NULL || index >= list->count)
        return;
    if (list->selected == index)
        return; /* 没变化，不必重绘 */

    list->selected = index;
    list_scroll_to_selection(list, list_visible_rows(list));
    LEANGL_Widget_Invalidate(&list->base);
}

void LEANGL_List_Select_Next(LEANGL_List_t *list) {
    if (list == NULL || list->count == 0)
        return;

    if (list->selected == LEANGL_LIST_NONE)
        LEANGL_List_Set_Selected(list, 0);
    else if ((uint32_t)list->selected + 1u < list->count)
        LEANGL_List_Set_Selected(list, (uint16_t)(list->selected + 1u));
    else
        LEANGL_List_Set_Selected(list, 0); /* 末尾回卷到第一条 */
}

void LEANGL_List_Select_Prev(LEANGL_List_t *list) {
    if (list == NULL || list->count == 0)
        return;

    if (list->selected == LEANGL_LIST_NONE)
        LEANGL_List_Set_Selected(list, (uint16_t)(list->count - 1u));
    else if (list->selected > 0)
        LEANGL_List_Set_Selected(list, (uint16_t)(list->selected - 1u));
    else
        LEANGL_List_Set_Selected(list, (uint16_t)(list->count - 1u));
}

uint16_t LEANGL_List_Get_Selected(const LEANGL_List_t *list) {
    return (list != NULL) ? list->selected : LEANGL_LIST_NONE;
}

void LEANGL_List_Set_Line_Height(LEANGL_List_t *list, uint16_t line_h) {
    if (list == NULL)
        return;

    list->line_h = line_h;
    list_scroll_to_selection(list, list_visible_rows(list));
    LEANGL_Widget_Invalidate(&list->base);
}

void LEANGL_List_Set_Colors(LEANGL_List_t *list, LEANGL_Color_t text_color,
                            LEANGL_Color_t sel_color, LEANGL_Color_t bg_color) {
    if (list == NULL)
        return;

    list->text_color = text_color;
    list->sel_color = sel_color;
    list->bg_color = bg_color;
    LEANGL_Widget_Invalidate(&list->base);
}

void LEANGL_List_Set_Selection_Bg(LEANGL_List_t *list, LEANGL_Color_t color,
                                  uint8_t enable) {
    if (list == NULL)
        return;

    list->sel_bg_color = color;
    list->show_sel_bg = enable ? 1u : 0u;
    LEANGL_Widget_Invalidate(&list->base);
}

void LEANGL_List_Set_Transparent(LEANGL_List_t *list, uint8_t is_transparent) {
    if (list == NULL)
        return;

    list->is_transparent = is_transparent;
    LEANGL_Widget_Invalidate(&list->base);
}

void LEANGL_List_Set_Ellipsis(LEANGL_List_t *list, uint8_t enable) {
    if (list == NULL)
        return;

    list->ellipsis = enable ? 1u : 0u;
    LEANGL_Widget_Invalidate(&list->base);
}