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

#ifndef LEANGL_LIST_H
#define LEANGL_LIST_H

#include "leangl_colors.h"
#include "leangl_widget.h"
#include <stdint.h>

/**
 * @file    leangl_list.h
 * @brief   列表控件：每项一行，选中项用不同颜色显示。
 *
 * 特点：
 *   - 每条记录占一行，行高默认取字体高度，可用 LEANGL_List_Set_Line_Height() 调整；
 *   - 选中项用 sel_color 绘制，其余项用 text_color；
 *   - 某一行放不下时，末尾用 "..." 省略（可用 LEANGL_List_Set_Ellipsis() 关闭）；
 *   - 条目数超过可见行数时自动滚动，保证选中项始终可见。
 *
 * 与 LEANGL_Button_t 一样，本控件不自己解析输入：应用在“上一条/下一条”事件
 * 到来时调用 LEANGL_List_Select_Prev() / LEANGL_List_Select_Next() 即可。
 *
 * 文本为 UTF-8，按码点绘制，中文与英文混排时各自使用自己的宽度。
 * 条目指针数组由调用方持有，控件只保存引用，不拷贝内容。
 */

/** @brief 没有选中项时 selected 的取值。 */
#define LEANGL_LIST_NONE 0xFFFFu

/** @brief 列表控件。 */
typedef struct {
    LEANGL_Widget_t base;         /**< 基类，必须是第一个成员 */
    const char *const *items;     /**< 条目数组，生命周期由调用方保证 */
    uint16_t count;               /**< 条目数 */
    uint16_t selected;            /**< 当前选中项；LEANGL_LIST_NONE 表示无选中 */
    uint16_t top;                 /**< 第一个可见条目的索引（滚动位置） */
    uint16_t line_h;              /**< 行高；0 表示使用字体高度 */
    uint16_t pad_x;               /**< 文本左侧留白（像素） */
    LEANGL_Color_t text_color;    /**< 未选中项文字颜色 */
    LEANGL_Color_t sel_color;     /**< 选中项文字颜色 */
    LEANGL_Color_t bg_color;      /**< 背景颜色，仅在 is_transparent 为 0 时生效 */
    LEANGL_Color_t sel_bg_color;  /**< 选中行背景色，仅在 show_sel_bg 非 0 时生效 */
    uint8_t is_transparent;       /**< 非 0 表示不填背景 */
    uint8_t show_sel_bg;          /**< 非 0 表示为选中行填充 sel_bg_color */
    uint8_t ellipsis;             /**< 非 0 表示放不下时末尾画 "..." */
} LEANGL_List_t;

/**
 * @brief 初始化列表控件。
 *
 * 不会自动加入控件树，初始化后需调用 LEANGL_Widget_Add()。
 * 默认：白字、青色选中字、不透明深色背景、选中行不加背景条、自动省略号、
 * 选中第 0 项。
 *
 * @param list  控件实例。
 * @param area  屏幕区域（闭区间）。
 * @param items 条目数组，可为 NULL。
 * @param count 条目数。
 */
void LEANGL_List_Init(LEANGL_List_t *list, const LEANGL_Rect_t *area,
                      const char *const *items, uint16_t count);

/** @brief 更换条目数组并标脏；count 为 0 时清除选择。 */
void LEANGL_List_Set_Items(LEANGL_List_t *list, const char *const *items,
                           uint16_t count);

/**
 * @brief 选中指定项并标脏，必要时滚动使其可见。
 *
 * 索引越界时忽略（保持原选择）。
 */
void LEANGL_List_Set_Selected(LEANGL_List_t *list, uint16_t index);

/** @brief 选中下一条；已在最后一条时回到第一条。 */
void LEANGL_List_Select_Next(LEANGL_List_t *list);

/** @brief 选中上一条；已在第一条时跳到最后一条。 */
void LEANGL_List_Select_Prev(LEANGL_List_t *list);

/** @brief 当前选中项索引；无选中时返回 LEANGL_LIST_NONE。 */
uint16_t LEANGL_List_Get_Selected(const LEANGL_List_t *list);

/**
 * @brief 设置行高并标脏。
 *
 * @param line_h 行高（像素）；传 0 表示回到“用字体高度”。
 */
void LEANGL_List_Set_Line_Height(LEANGL_List_t *list, uint16_t line_h);

/**
 * @brief 设置文字与背景颜色并标脏。
 *
 * @param text_color   未选中项文字颜色。
 * @param sel_color    选中项文字颜色。
 * @param bg_color     背景颜色。
 */
void LEANGL_List_Set_Colors(LEANGL_List_t *list, LEANGL_Color_t text_color,
                            LEANGL_Color_t sel_color, LEANGL_Color_t bg_color);

/**
 * @brief 设置选中行的背景条并标脏。
 *
 * @param color 选中行背景色。
 * @param enable 非 0 启用；0 关闭（选中行只靠文字颜色区分）。
 */
void LEANGL_List_Set_Selection_Bg(LEANGL_List_t *list, LEANGL_Color_t color,
                                  uint8_t enable);

/** @brief 设置背景是否透明并标脏。 */
void LEANGL_List_Set_Transparent(LEANGL_List_t *list, uint8_t is_transparent);

/** @brief 设置文本是否自动加省略号并标脏。 */
void LEANGL_List_Set_Ellipsis(LEANGL_List_t *list, uint8_t enable);

#endif /* LEANGL_LIST_H */
