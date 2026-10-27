#ifndef LEANGL_SCROLL_LABEL_H
#define LEANGL_SCROLL_LABEL_H

#include "leangl_colors.h"
#include "leangl_widget.h"
#include <stdint.h>

/**
 * @file    leangl_scroll_label.h
 * @brief   滚动标签控件：只占一行，文字跑马灯式循环滚动。
 *
 * 与 LEANGL_Label_t 的区别只有两点：
 *   - **只绘制一行**，'\n' 当作结束符，不会换行；
 *   - 文本比控件宽时自动滚动，循环播放。
 *
 * 循环模型：
 *      [ 文本 ][ 间隙 ][ 文本 ][ 间隙 ] ...
 *
 * 文本末尾先空出 gap 像素（默认 4 个字符宽），然后紧跟文本开头。绘制时同时
 * 画“当前这份”和“下一份”，因此屏幕上看到的是连续的跑马灯，而不是“末尾消失了
 * 才从开头突然跳出来”。
 *
 * 滚动周期 = 文本宽度 + gap；偏移到达周期后归零，实现无缝衔接。
 *
 * 其余行为一致：UTF-8 解码、按码点逐字绘制、可选背景色、可设文字颜色。
 *
 * 滚动由控件自身的 event_cb 驱动，每个事件周期（LEANGL_EVENT_PERIOD_MS）
 * 前进 step 像素并请求重绘，因此不需要应用额外喂时间。
 *
 * 注意：滚动粒度是**按字符**的——被区域边缘切掉一半的字不会绘制。库没有像素级
 * 裁剪能力，这样既简单又保证不越界写到邻居控件。
 *
 * 文本比控件窄时不会滚动，原地静止显示。
 */

/** @brief 滚动方向。 */
typedef enum {
    LEANGL_SCROLL_LEFT = 0, /**< 文字向左移动（内容从右往左流过）。 */
    LEANGL_SCROLL_RIGHT = 1 /**< 文字向右移动（内容从左往右流过）。 */
} LEANGL_Scroll_Dir_t;

/** @brief 滚动标签控件。 */
typedef struct {
    LEANGL_Widget_t base;      /**< 基类，必须是第一个成员 */
    const char *text;          /**< UTF-8 文本指针，生命周期由调用方保证 */
    LEANGL_Color_t text_color; /**< 文字颜色 */
    LEANGL_Color_t bg_color;   /**< 背景颜色，仅在 is_transparent 为 0 时生效 */
    uint16_t offset;           /**< 当前滚动偏移（像素），范围 [0, text_w + gap) */
    uint16_t step;             /**< 每个事件周期前进的像素数；0 表示不滚动 */
    uint16_t gap;              /**< 文本末尾到下一份开头的像素间隙 */
    uint16_t text_w;           /**< 文本宽度（像素），由库维护（首次使用时测量并缓存） */
    uint8_t is_transparent;    /**< 非 0 表示不填背景 */
    uint8_t direction;         /**< LEANGL_SCROLL_DIR 取值 */
} LEANGL_Scroll_Label_t;

/**
 * @brief 初始化滚动标签。
 *
 * 不会自动加入控件树，初始化后需调用 LEANGL_Widget_Add()。
 * 默认：白字、透明背景、向左滚动、每周期前进 1 像素、末尾间隙 4 个字符宽。
 *
 * @param sl     控件实例。
 * @param area   屏幕区域；高度通常设为一行。
 * @param text   UTF-8 文本；可为 NULL。
 * @param dir    滚动方向。
 */
void LEANGL_Scroll_Label_Init(LEANGL_Scroll_Label_t *sl,
                              const LEANGL_Rect_t *area, const char *text,
                              LEANGL_Scroll_Dir_t dir);

/** @brief 更换文本并标脏；偏移会归零，从头开始滚。 */
void LEANGL_Scroll_Label_Set_Text(LEANGL_Scroll_Label_t *sl, const char *text);

/** @brief 更换文字颜色并标脏。 */
void LEANGL_Scroll_Label_Set_Text_Color(LEANGL_Scroll_Label_t *sl,
                                        LEANGL_Color_t color);

/** @brief 更换背景颜色并标脏。 */
void LEANGL_Scroll_Label_Set_Bg_Color(LEANGL_Scroll_Label_t *sl,
                                      LEANGL_Color_t color);

/** @brief 设置背景是否透明并标脏。 */
void LEANGL_Scroll_Label_Set_Transparent(LEANGL_Scroll_Label_t *sl,
                                          uint8_t is_transparent);

/**
 * @brief 切换滚动方向并标脏。
 *
 * @param dir LEANGL_SCROLL_LEFT 或 LEANGL_SCROLL_RIGHT。
 */
void LEANGL_Scroll_Label_Set_Direction(LEANGL_Scroll_Label_t *sl,
                                        LEANGL_Scroll_Dir_t dir);

/** @brief 取当前滚动方向。 */
LEANGL_Scroll_Dir_t LEANGL_Scroll_Label_Get_Direction(
    const LEANGL_Scroll_Label_t *sl);

/**
 * @brief 设置每个事件周期的滚动步长并标脏。
 *
 * @param step 像素数；0 表示停止滚动（静止显示当前偏移处的画面）。
 */
void LEANGL_Scroll_Label_Set_Step(LEANGL_Scroll_Label_t *sl, uint16_t step);

/**
 * @brief 设置文本末尾到下一份开头的间隙并标脏。
 *
 * 这是实现“末尾空几格再接着显示开头”的方式：间隙越大，两轮文字之间隔得越开。
 * 传 0 则末尾直接紧跟开头。
 *
 * @param gap 间隙（像素）。通常取几个字符宽，例如
 *            `LEANGL_Scroll_Label_Set_Gap_Chars(sl, 4)`。
 */
void LEANGL_Scroll_Label_Set_Gap(LEANGL_Scroll_Label_t *sl, uint16_t gap);

/**
 * @brief 按“字符个数”设置间隙，内部换算成像素并标脏。
 *
 * 用当前字体的空格宽度换算；字体里没有空格字形时退化为按字高的一半估算，
 * 不会出现“设置了间隙却看不到”的情况。
 *
 * @param chars 间隙包含的字符个数；0 表示末尾紧跟开头。
 */
void LEANGL_Scroll_Label_Set_Gap_Chars(LEANGL_Scroll_Label_t *sl,
                                       uint16_t chars);

/** @brief 取当前间隙（像素）。 */
uint16_t LEANGL_Scroll_Label_Get_Gap(const LEANGL_Scroll_Label_t *sl);

/** @brief 把偏移归零，下次绘制从文本开头开始。 */
void LEANGL_Scroll_Label_Restart(LEANGL_Scroll_Label_t *sl);

#endif /* LEANGL_SCROLL_LABEL_H */
