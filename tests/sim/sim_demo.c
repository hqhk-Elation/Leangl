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
 * @file    sim_demo.c
 * @brief   模拟器的演示界面。
 *
 * 实现 sim.h 里的 leangl_sim_ui()。想换成自己的界面时，复制本文件并
 * 改写 demo_build / demo_pointer / demo_key 即可，引擎（sim_win32.c）
 * 不用动。
 *
 * 布局按屏幕尺寸等比例计算，因此配合 --size 可以看任意分辨率下的效果，
 * 例如：leangl_sim --size 320x240
 *
 * 交互：
 *   键盘 S / W   —— 列表选中下一条 / 上一条（循环）
 *   键盘 D       —— 翻转滚动标签的滚动方向
 *   鼠标左键     —— 点击按钮
 */

#include <stdio.h>
#include <string.h>

#include "leangl.h"
#include "leangl_colors.h"
#include "sim.h"

/* ================================================================== */
/* 自定义控件：纯色面板                                                 */
/* ================================================================== */

/**
 * @brief 把 LEANGL_Widget_t 放在最前面即可直接交给控件树使用。
 */
typedef struct {
    LEANGL_Widget_t base;
    LEANGL_Color_t color;
} Demo_Panel_t;

static void panel_draw(LEANGL_Widget_t *w) {
    Demo_Panel_t *p = (Demo_Panel_t *)w;
    LEANGL_Fill_Rect(&w->area, p->color);
}

/* ================================================================== */
/* 控件实例                                                            */
/* ================================================================== */

static Demo_Panel_t g_bg;     /* 全屏背景，必须最先加入 */
static Demo_Panel_t g_header; /* 顶部标题栏 */
static LEANGL_Label_t g_title;
static LEANGL_Label_t g_info;
static LEANGL_Label_t g_count;
static LEANGL_List_t g_list;
static LEANGL_Scroll_Label_t g_scroll;
static LEANGL_Button_t g_btn_a;
static LEANGL_Button_t g_btn_b;

static LEANGL_Button_t *g_pressed = NULL; /* 当前按下的按钮 */

static int g_clicks = 0;
static int g_toggled = 0;
static uint32_t g_last_info_ms = 0;

/* snprintf 按**字节**截断，而一个中文字占 3 字节。缓冲太小时会把多字节
   字符劈成两半，留下一个孤立的 UTF-8 首字节；解码器发现续字节不是
   10xxxxxx，会丢弃整个序列，于是“后面的内容全部消失”。
   这里留足余量，正常内容不会碰到上限。 */
static char g_count_text[64];
static char g_info_text[64];

/* 列表条目：故意混入超长中文与英文，用来验证末尾省略号。 */
static const char *const k_list_items[] = {
    "Item 01 - short",
    "Item 02 - 中文条目宽度不同",
    "Item 03 - a fairly long English entry that cannot fit at all",
    "Item 04 - mixed 中英 mixed 混排 test",
    "Item 05 - 再长一点的内容用来触发省略号显示效果",
    "Item 05 - 再长一点的内容用来触发省略号显示效果",
    "Item 05 - 再长一点的内容用来触发省略号显示效果",
    "Item 05 - 再长一点的内容用来触发省略号显示效果",
    "Item 05 - 再长一点的内容用来触发省略号显示效果",
    "Item 05 - 再长一点的内容用来触发省略号显示效果",
    "Item 05 - 再长一点的内容用来触发省略号显示效果",
    "Item 05 - 再长一点的内容用来触发省略号显示效果",
    "Item 06 - end",
};
static const uint16_t k_list_count =
    (uint16_t)(sizeof k_list_items / sizeof k_list_items[0]);

/* 滚动标签的文本：明显比控件宽，才会滚动。 */
static const char k_scroll_text[] =
    "LeanGL scroll label 循环滚动演示 - 滚到末尾自动从头开始 ... ";

/* ================================================================== */
/* 文本刷新                                                            */
/* ================================================================== */

static void demo_refresh_info(uint32_t ms);

static void demo_refresh_count(void) {
    snprintf(g_count_text, sizeof g_count_text, "Clicks: %d  dir=%s", g_clicks,
             LEANGL_Scroll_Label_Get_Direction(&g_scroll) == LEANGL_SCROLL_LEFT
                 ? "LEFT"
                 : "RIGHT");
    LEANGL_Label_Set_Text(&g_count, g_count_text);
}

static void demo_refresh_info(uint32_t ms) {
    snprintf(g_info_text, sizeof g_info_text, "%s  %ux%u  sel=%u/%u",
             LEANGL_Sim_Mode(), (unsigned)LEANGL_Sim_Width(),
             (unsigned)LEANGL_Sim_Height(),
             (unsigned)LEANGL_List_Get_Selected(&g_list), (unsigned)k_list_count);
    LEANGL_Label_Set_Text(&g_info, g_info_text);
    g_last_info_ms = ms;
}

/** @brief 翻转滚动方向，并同步刷新显示方向的那一行。 */
static void demo_flip_direction(void) {
    LEANGL_Scroll_Label_Set_Direction(
        &g_scroll,
        LEANGL_Scroll_Label_Get_Direction(&g_scroll) == LEANGL_SCROLL_LEFT
            ? LEANGL_SCROLL_RIGHT
            : LEANGL_SCROLL_LEFT);
    demo_refresh_count();
}

/* ================================================================== */
/* 按钮回调                                                            */
/* ================================================================== */

static void on_click_a(LEANGL_Button_t *button) {
    (void)button;
    g_clicks++;
    demo_refresh_count();
}

static void on_click_b(LEANGL_Button_t *button) {
    g_toggled = !g_toggled;
    /* 文本长度变化正好用来验证背景控件能否消除残影。 */
    LEANGL_Button_Set_Text(button, g_toggled ? "STATE: ON" : "TOGGLE");
}

/* ================================================================== */
/* 布局                                                                */
/* ================================================================== */

static LEANGL_Rect_t demo_rect(uint16_t x1, uint16_t y1, uint16_t x2,
                               uint16_t y2) {
    LEANGL_Rect_t r;
    r.x1 = x1;
    r.y1 = y1;
    r.x2 = x2;
    r.y2 = y2;
    return r;
}

static void demo_build(uint16_t w, uint16_t h) {
    LEANGL_Rect_t full;
    LEANGL_Rect_t header;
    LEANGL_Rect_t title;
    LEANGL_Rect_t info;
    LEANGL_Rect_t list;
    LEANGL_Rect_t scroll;
    LEANGL_Rect_t count;
    LEANGL_Rect_t area_a;
    LEANGL_Rect_t area_b;

    uint16_t margin = (uint16_t)(w / 12u);
    uint16_t header_h = (uint16_t)(h / 8u);
    uint16_t text_h = 16; /* 字模高度 */
    uint16_t list_top;
    uint16_t list_bottom;
    uint16_t scroll_top;
    uint16_t btn_h;
    uint16_t y_a;
    uint16_t y_b;

    if (margin < 4)
        margin = 4;
    if (header_h < text_h + 6)
        header_h = (uint16_t)(text_h + 6);

    /* 纵向分区：标题栏 / 状态行 / 列表 / 滚动行 / 计数行 / 两个按钮 */
    list_top = (uint16_t)(header_h + 6 + text_h + 6);
    list_bottom = (uint16_t)(h * 56u / 100u);
    scroll_top = (uint16_t)(list_bottom + 4);

    btn_h = (uint16_t)(h * 10u / 100u);
    if (btn_h < text_h + 8)
        btn_h = (uint16_t)(text_h + 8);
    y_b = (uint16_t)(h - btn_h - 4);
    y_a = (uint16_t)(y_b - btn_h - 6);

    /* 列表至少留 2 行的高度。 */
    if (list_bottom < (uint32_t)list_top + text_h * 2u)
        list_bottom = (uint16_t)(list_top + text_h * 2u);

    /* 小屏时滚动行会挤到按钮上方，压到列表里也不能重叠。 */
    if ((uint32_t)scroll_top + text_h * 2u + 10u >= y_a)
        scroll_top = (uint16_t)(list_bottom + 2);

    full = demo_rect(0, 0, (uint16_t)(w - 1), (uint16_t)(h - 1));
    header = demo_rect(0, 0, (uint16_t)(w - 1), (uint16_t)(header_h - 1));
    title = demo_rect(margin, (uint16_t)((header_h - text_h) / 2u),
                      (uint16_t)(w - 1 - margin),
                      (uint16_t)((header_h + text_h) / 2u));
    info = demo_rect(margin, (uint16_t)(header_h + 6),
                     (uint16_t)(w - 1 - margin),
                     (uint16_t)(header_h + 6 + text_h));
    list = demo_rect(margin, list_top, (uint16_t)(w - 1 - margin), list_bottom);
    scroll = demo_rect(margin, scroll_top, (uint16_t)(w - 1 - margin),
                       (uint16_t)(scroll_top + text_h - 1));
    count = demo_rect(margin, (uint16_t)(scroll_top + text_h + 4),
                      (uint16_t)(w - 1 - margin),
                      (uint16_t)(scroll_top + text_h + 4 + text_h - 1));
    area_a = demo_rect(margin, y_a, (uint16_t)(w - 1 - margin),
                       (uint16_t)(y_a + btn_h));
    area_b = demo_rect(margin, y_b, (uint16_t)(w - 1 - margin),
                       (uint16_t)(y_b + btn_h));

    /* ① 全屏背景：每个被刷新的像素带都会被它铺满，避免分块残留。 */
    LEANGL_Widget_Init(&g_bg.base, &full, panel_draw, 0);
    g_bg.color = LEANGL_Color_Hex(0x101820);
    LEANGL_Widget_Add(&g_bg.base);

    /* ② 顶部标题栏 */
    LEANGL_Widget_Init(&g_header.base, &header, panel_draw, 0);
    g_header.color = LEANGL_Color_Hex(0x1E5AA8);
    LEANGL_Widget_Add(&g_header.base);

    /* ③ 文本 */
    LEANGL_Label_Init(&g_title, &title, "LeanGL Simulator");
    LEANGL_Label_Set_Text_Color(&g_title, LEANGL_COLOR_WHITE);
    LEANGL_Label_Set_Transparent(&g_title, 1);
    LEANGL_Widget_Add(&g_title.base);

    g_info_text[0] = '\0';
    LEANGL_Label_Init(&g_info, &info, g_info_text);
    LEANGL_Label_Set_Text_Color(&g_info, LEANGL_Color_Hex(0x60C0FF));
    LEANGL_Label_Set_Transparent(&g_info, 1);
    LEANGL_Widget_Add(&g_info.base);

    /* ④ 列表：每项一行，选中项异色，超宽自动省略。 */
    LEANGL_List_Init(&g_list, &list, k_list_items, k_list_count);
    LEANGL_List_Set_Colors(&g_list, LEANGL_Color_Hex(0xC8D4E8),
                           LEANGL_COLOR_YELLOW, LEANGL_Color_Hex(0x181C24));
    LEANGL_List_Set_Selection_Bg(&g_list, LEANGL_Color_Hex(0x243352), 1);
    LEANGL_List_Set_Ellipsis(&g_list, 1);
    LEANGL_Widget_Add(&g_list.base);

    /* ⑤ 滚动标签：单行、循环滚动。 */
    LEANGL_Scroll_Label_Init(&g_scroll, &scroll, k_scroll_text,
                              LEANGL_SCROLL_LEFT);
    LEANGL_Scroll_Label_Set_Text_Color(&g_scroll, LEANGL_Color_Hex(0x60FFA0));
    LEANGL_Scroll_Label_Set_Transparent(&g_scroll, 1);
    LEANGL_Scroll_Label_Set_Step(&g_scroll, 2); /* 2px/周期，看得清 */
    LEANGL_Widget_Add(&g_scroll.base);

    /* ⑥ 计数行 */
    g_count_text[0] = '\0';
    LEANGL_Label_Init(&g_count, &count, g_count_text);
    LEANGL_Label_Set_Text_Color(&g_count, LEANGL_Color_Hex(0xC8D4E8));
    LEANGL_Label_Set_Transparent(&g_count, 1);
    LEANGL_Widget_Add(&g_count.base);
    demo_refresh_count();

    /* ⑦ 按钮 */
    LEANGL_Button_Init(&g_btn_a, &area_a, "COUNT +1");
    LEANGL_Button_On_Click(&g_btn_a, on_click_a);
    LEANGL_Widget_Add(&g_btn_a.base);

    LEANGL_Button_Init(&g_btn_b, &area_b, "TOGGLE");
    LEANGL_Button_Set_Colors(&g_btn_b, LEANGL_Color_Hex(0x2A3A50),
                             LEANGL_Color_Hex(0xC06000), LEANGL_COLOR_WHITE);
    LEANGL_Button_On_Click(&g_btn_b, on_click_b);
    LEANGL_Widget_Add(&g_btn_b.base);

    demo_refresh_info(0);
}

/* ================================================================== */
/* 每帧回调                                                            */
/* ================================================================== */

static void demo_update(uint32_t ms) {
    /* 每 500ms 刷新一次状态行，演示周期性内容变化。 */
    if ((uint32_t)(ms - g_last_info_ms) >= 500u) {
        char text[64];

        snprintf(text, sizeof text, "%s  %ux%u  sel=%u/%u",
                 LEANGL_Sim_Mode(), (unsigned)LEANGL_Sim_Width(),
                 (unsigned)LEANGL_Sim_Height(),
                 (unsigned)LEANGL_List_Get_Selected(&g_list),
                 (unsigned)k_list_count);

        if (strcmp(text, g_info_text) != 0) {
            memcpy(g_info_text, text, sizeof g_info_text);
            LEANGL_Label_Set_Text(&g_info, g_info_text);
        }
        g_last_info_ms = ms;
    }
}

/* ================================================================== */
/* 鼠标                                                                */
/* ================================================================== */

static LEANGL_Button_t *demo_hit_test(int x, int y) {
    LEANGL_Button_t *buttons[2];
    int i;

    buttons[0] = &g_btn_a;
    buttons[1] = &g_btn_b;

    for (i = 0; i < 2; i++) {
        const LEANGL_Rect_t *a = &buttons[i]->base.area;

        if (x >= (int)a->x1 && x <= (int)a->x2 && y >= (int)a->y1 &&
            y <= (int)a->y2)
            return buttons[i];
    }
    return NULL;
}

static void demo_pointer(int x, int y, int pressed) {
    LEANGL_Button_t *hit = demo_hit_test(x, y);

    if (pressed) {
        if (g_pressed == NULL) {
            if (hit != NULL) {
                g_pressed = hit;
                LEANGL_Button_Press(hit);
            }
        } else if (g_pressed != hit) {
            /* 手指移出按钮：取消，不触发点击。 */
            LEANGL_Button_Cancel(g_pressed);
            g_pressed = NULL;
        }
        return;
    }

    if (g_pressed == NULL)
        return;

    if (g_pressed == hit)
        LEANGL_Button_Release(g_pressed); /* 触发 on_click */
    else
        LEANGL_Button_Cancel(g_pressed);

    g_pressed = NULL;
}

/* ================================================================== */
/* 键盘                                                                */
/* ================================================================== */

static void demo_key(int ch) {
    switch (ch) {
    case 's': /* 下一个 */
        LEANGL_List_Select_Next(&g_list);
        demo_refresh_info(g_last_info_ms);
        break;

    case 'w': /* 上一个 */
        LEANGL_List_Select_Prev(&g_list);
        demo_refresh_info(g_last_info_ms);
        break;

    case 'd': /* 翻转滚动方向 */
        demo_flip_direction();
        break;

    default:
        break;
    }
}

/* ================================================================== */
/* 注册                                                                */
/* ================================================================== */

const LEANGL_Sim_UI_t *leangl_sim_ui(void) {
    static const LEANGL_Sim_UI_t ui = {
        "demo",
        demo_build,
        demo_update,
        demo_pointer,
        demo_key,
    };

    return &ui;
}
