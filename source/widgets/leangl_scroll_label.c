/**
 * @file    leangl_scroll_label.c
 * @brief   滚动标签：单行、循环跑马灯。
 *
 * 循环模型：
 *
 *     |<------- 一个周期 period = text_w + gap ------->|
 *     [====== 文本 ======][ gap ][====== 文本 ======][ gap ] ...
 *
 * 偏移量 offset 在 [0, period) 内递增，到达 period 后归零。因为周期里已经留出了
 * gap 的空隙，归零时“下一份”恰好落在“当前这份”原先的位置，屏幕上看到的是连贯
 * 的循环，而不是文字消失后突然又从开头冒出来。
 *
 * 绘制时同时画两份：当前这份，以及它的相邻副本（LEFT 方向在右侧，RIGHT 方向在
 * 左侧）。只有副本可能进入可见区域时才画，两份都按区域做字符级裁剪。
 */

#include "leangl_scroll_label.h"
#include "leangl_colors.h"
#include "leangl_draw_char.h"
#include "leangl_draw_shape.h"
#include "leangl_font.h"
#include <stddef.h>
#include <stdint.h>

/* 默认间隙：末尾空 4 个字符宽。 */
#define SCROLL_DEFAULT_GAP_CHARS 4u

/* ------------------------------------------------------------------ */
/* 辅助                                                                */
/* ------------------------------------------------------------------ */

/** @brief 控件内容宽度（像素）；区域非法时为 0。 */
static uint16_t scroll_area_w(const LEANGL_Scroll_Label_t *sl) {
    if (sl->base.area.x2 < sl->base.area.x1)
        return 0;
    return (uint16_t)(sl->base.area.x2 - sl->base.area.x1 + 1u);
}

/**
 * @brief 一个字符的估计宽度（像素），用于把“几个字符”换算成像素。
 *
 * 优先取空格的实际宽度；字体里没有空格字形时退化为字高的一半，
 * 保证换算结果不会是 0。
 */
static uint16_t scroll_char_w(void) {
    uint16_t w = LEANGL_Char_Size((LEANGL_CodePoint_t)' ').w;

    if (w == 0)
        w = (uint16_t)(LEANGL_Font_Height() / 2u);
    if (w == 0)
        w = 8; /* 字体未注册时的占位值 */

    return w;
}

/** @brief 按字符个数换算像素间隙。 */
static uint16_t scroll_gap_from_chars(uint16_t chars) {
    uint32_t px = (uint32_t)scroll_char_w() * chars;

    return (uint16_t)(px > UINT16_MAX ? UINT16_MAX : px);
}

/**
 * @brief 取文本宽度，首次使用时测量并缓存。
 *
 * 滚动标签每个事件周期和每次重绘都要用到文本宽度；若不缓存，一段长文本
 * 会在一帧里被完整遍历、逐字取模好几次，成为滚动动画的主要开销。
 *
 * 文本或字体变化时缓存会被清零（Set_Text 显式清零；字体在控件创建后才
 * 注册的用法则依赖“宽度为 0 就重新测量”的懒加载）。
 */
static uint16_t scroll_text_width(LEANGL_Scroll_Label_t *sl) {
    if (sl->text_w == 0 && sl->text != NULL)
        sl->text_w = LEANGL_Font_Text_Width(sl->text);

    return sl->text_w;
}

/**
 * @brief 一个滚动周期的长度：文本宽度 + 间隙。
 *
 * 这个值同时决定“多远归零”和“下一份画在哪”。
 */
static uint16_t scroll_period(LEANGL_Scroll_Label_t *sl) {
    uint32_t period = (uint32_t)scroll_text_width(sl) + sl->gap;

    return (uint16_t)(period > UINT16_MAX ? UINT16_MAX : period);
}

/**
 * @brief 文本是否需要滚动。
 *
 * 用**文本宽度**与控件宽度比较：文本放得下就静止显示，不因为间隙而误判。
 * 一旦需要滚动，周期才按“文本 + 间隙”计算，于是末尾自然留出空隙再接开头。
 */
static uint8_t scroll_should_move(LEANGL_Scroll_Label_t *sl) {
    if (sl->text == NULL || sl->step == 0)
        return 0;
    if (scroll_area_w(sl) == 0)
        return 0;

    return (uint8_t)(scroll_text_width(sl) > scroll_area_w(sl));
}

/* ------------------------------------------------------------------ */
/* 事件：推进偏移                                                      */
/* ------------------------------------------------------------------ */

static uint8_t scroll_event(LEANGL_Widget_t *w) {
    LEANGL_Scroll_Label_t *sl = (LEANGL_Scroll_Label_t *)w;
    uint16_t period;

    if (!scroll_should_move(sl))
        return 0; /* 放得下或已停止，不需要重绘 */

    period = scroll_period(sl);

    sl->offset = (uint16_t)(sl->offset + sl->step);

    /* 走完一个周期（文本 + 间隙）就回到起点：此时下一份恰好到达起点，
       衔接处不会出现跳变。 */
    if (sl->offset >= period)
        sl->offset = (uint16_t)(sl->offset % period);

    return 1; /* 请求重绘本控件区域 */
}

/* ------------------------------------------------------------------ */
/* 绘制                                                                */
/* ------------------------------------------------------------------ */

static void scroll_draw(LEANGL_Widget_t *w) {
    LEANGL_Scroll_Label_t *sl = (LEANGL_Scroll_Label_t *)w;
    LEANGL_Rect_t area = w->area;
    LEANGL_Text_Line_t opt;
    uint16_t area_w;
    uint16_t text_w;
    uint16_t period;
    int32_t start_x;
    int32_t next_x;

    if (area.x2 < area.x1 || area.y2 < area.y1)
        return;

    /* 与 Label 一致：不透明时先铺底，避免文本变短留下残影。 */
    if (!sl->is_transparent)
        LEANGL_Fill_Rect(&area, sl->bg_color);

    if (sl->text == NULL || sl->text[0] == '\0')
        return;

    area_w = (uint16_t)(area.x2 - area.x1 + 1u);
    text_w = scroll_text_width(sl);
    period = scroll_period(sl);

    /* 放得下就静止显示（不需要第二份）；与 scroll_should_move() 用同一个判据，
       避免"判定静止但绘制走滚动分支"这种不一致。 */
    if (text_w <= area_w) {
        LEANGL_Set_Char_Color(sl->text_color);

        opt.x = (int32_t)area.x1;
        opt.y = area.y1;
        opt.clip_x1 = area.x1;
        opt.clip_x2 = area.x2;
        opt.ellipsis = 0;
        (void)LEANGL_Draw_Text_Line(sl->text, &opt);
        return;
    }

    /* 算出当前这份的起点。
       LEFT  ：offset = 0 时从区域左边开始，逐渐往左移出；
       RIGHT ：offset = 0 时整段位于区域左边之外，逐渐往右滑入。 */
    if (sl->direction == LEANGL_SCROLL_RIGHT)
        start_x = (int32_t)area.x1 - (int32_t)text_w + (int32_t)sl->offset;
    else
        start_x = (int32_t)area.x1 - (int32_t)sl->offset;

    /* 相邻副本的起点：LEFT 方向在右侧，RIGHT 方向在左侧。
       因为 period 里已含 gap，两份之间自然隔出那个间隙。 */
    if (sl->direction == LEANGL_SCROLL_RIGHT)
        next_x = start_x - (int32_t)period;
    else
        next_x = start_x + (int32_t)period;

    LEANGL_Set_Char_Color(sl->text_color);

    opt.y = area.y1;
    opt.clip_x1 = area.x1;
    opt.clip_x2 = area.x2;
    opt.ellipsis = 0; /* 滚动标签不加省略号：多出来的内容靠滚动看到 */

    /* 先画当前这份。 */
    opt.x = start_x;
    (void)LEANGL_Draw_Text_Line(sl->text, &opt);

    /* 再画相邻副本——只当它可能进入可见区域时才画，
       避免每次重绘都多跑一遍取模与解码。 */
    if (next_x <= (int32_t)area.x2 &&
        next_x + (int32_t)text_w > (int32_t)area.x1) {
        opt.x = next_x;
        (void)LEANGL_Draw_Text_Line(sl->text, &opt);
    }
}

/* ------------------------------------------------------------------ */
/* 初始化与配置                                                        */
/* ------------------------------------------------------------------ */

void LEANGL_Scroll_Label_Init(LEANGL_Scroll_Label_t *sl,
                              const LEANGL_Rect_t *area, const char *text,
                              LEANGL_Scroll_Dir_t dir) {
    if (sl == NULL)
        return;

    LEANGL_Widget_Init(&sl->base, area, scroll_draw, scroll_event);

    sl->text = text;
    sl->text_color = LEANGL_COLOR_WHITE;
    sl->bg_color = LEANGL_COLOR_BLACK;
    sl->offset = 0;
    sl->step = 1;
    sl->gap = scroll_gap_from_chars(SCROLL_DEFAULT_GAP_CHARS);
    sl->text_w = 0; /* 懒测量：字体可能在控件创建之后才注册 */
    sl->is_transparent = 1; /* 滚动标签通常浮在别人身上，默认透明 */
    sl->direction = (uint8_t)dir;
}

void LEANGL_Scroll_Label_Set_Text(LEANGL_Scroll_Label_t *sl,
                                  const char *text) {
    if (sl == NULL)
        return;

    sl->text = text;
    sl->offset = 0; /* 换了内容就从头开始 */
    sl->text_w = 0; /* 宽度失效，下次使用时重新测量 */
    LEANGL_Widget_Invalidate(&sl->base);
}

void LEANGL_Scroll_Label_Set_Text_Color(LEANGL_Scroll_Label_t *sl,
                                        LEANGL_Color_t color) {
    if (sl == NULL)
        return;

    sl->text_color = color;
    LEANGL_Widget_Invalidate(&sl->base);
}

void LEANGL_Scroll_Label_Set_Bg_Color(LEANGL_Scroll_Label_t *sl,
                                      LEANGL_Color_t color) {
    if (sl == NULL)
        return;

    sl->bg_color = color;
    LEANGL_Widget_Invalidate(&sl->base);
}

void LEANGL_Scroll_Label_Set_Transparent(LEANGL_Scroll_Label_t *sl,
                                         uint8_t is_transparent) {
    if (sl == NULL)
        return;

    sl->is_transparent = is_transparent;
    LEANGL_Widget_Invalidate(&sl->base);
}

void LEANGL_Scroll_Label_Set_Direction(LEANGL_Scroll_Label_t *sl,
                                       LEANGL_Scroll_Dir_t dir) {
    if (sl == NULL || sl->direction == (uint8_t)dir)
        return;

    sl->direction = (uint8_t)dir;
    sl->offset = 0; /* 换方向后从起点重新开始，避免停在奇怪的位置 */
    LEANGL_Widget_Invalidate(&sl->base);
}

LEANGL_Scroll_Dir_t LEANGL_Scroll_Label_Get_Direction(
    const LEANGL_Scroll_Label_t *sl) {
    if (sl == NULL)
        return LEANGL_SCROLL_LEFT;
    return (LEANGL_Scroll_Dir_t)sl->direction;
}

void LEANGL_Scroll_Label_Set_Step(LEANGL_Scroll_Label_t *sl, uint16_t step) {
    if (sl == NULL)
        return;

    sl->step = step;
    LEANGL_Widget_Invalidate(&sl->base);
}

void LEANGL_Scroll_Label_Set_Gap(LEANGL_Scroll_Label_t *sl, uint16_t gap) {
    uint16_t period;

    if (sl == NULL)
        return;

    sl->gap = gap;

    /* 周期变短后，旧偏移可能已经越过新的周期：收敛回合法范围，
       否则下一帧会停在一个“看不到任何文字”的位置。 */
    period = scroll_period(sl);
    if (period != 0 && sl->offset >= period)
        sl->offset = (uint16_t)(sl->offset % period);

    LEANGL_Widget_Invalidate(&sl->base);
}

void LEANGL_Scroll_Label_Set_Gap_Chars(LEANGL_Scroll_Label_t *sl,
                                       uint16_t chars) {
    LEANGL_Scroll_Label_Set_Gap(sl, scroll_gap_from_chars(chars));
}

uint16_t LEANGL_Scroll_Label_Get_Gap(const LEANGL_Scroll_Label_t *sl) {
    return (sl != NULL) ? sl->gap : 0u;
}

void LEANGL_Scroll_Label_Restart(LEANGL_Scroll_Label_t *sl) {
    if (sl == NULL)
        return;

    sl->offset = 0;
    LEANGL_Widget_Invalidate(&sl->base);
}
