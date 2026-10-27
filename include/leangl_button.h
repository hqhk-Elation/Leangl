#ifndef LEANGL_BUTTON_H
#define LEANGL_BUTTON_H

#include "leangl_colors.h"
#include "leangl_widget.h"
#include <stdint.h>

/**
 * @file    leangl_button.h
 * @brief   按钮控件。
 *
 * 按钮自己维护“普通 / 按下”两个状态，并根据输入事件完成状态转换：
 *   - PRESS：普通 → 按下；
 *   - RELEASE：按下 → 普通，并触发 on_click；
 *   - CANCEL：按下 → 普通，不触发 on_click（例如手指移出按钮）。
 *
 * 输入层只需在对应时机调用 LEANGL_Button_Press() / Release() / Cancel()，
 * 不需要自己判断按钮当前该处于什么状态。重复或乱序的事件会被忽略。
 */

/* 前置声明：回调签名需要引用该标签，但结构体在回调之后定义。 */
struct LEANGL_Button;

/** @brief 按钮回调。 */
typedef void (*LEANGL_Button_Cb)(struct LEANGL_Button *button);

/**
 * @brief 按钮控件。
 *
 * 注：本结构体含指向自身的回调，必须有标签名供回调签名引用，因此写成
 *     带标签的单条 typedef，而不是完全匿名的 typedef struct {}。
 */
typedef struct LEANGL_Button {
    LEANGL_Widget_t base;      /**< 基类，必须是第一个成员 */
    const char *text;          /**< 按钮文本，生命周期由调用方保证 */
    LEANGL_Color_t bg;         /**< 正常背景色 */
    LEANGL_Color_t pressed_bg; /**< 按下时的背景色 */
    LEANGL_Color_t text_color; /**< 文字颜色 */
    uint8_t pressed;           /**< 当前是否按下 */
    uint16_t text_w;           /**< 文本宽度（像素），由库维护 */
    uint8_t text_h;            /**< 文本高度（像素），由库维护 */
    LEANGL_Button_Cb on_click; /**< 点击回调，可为 NULL */
} LEANGL_Button_t;

/**
 * @brief 初始化按钮控件。
 *
 * 不会自动加入控件表；默认深灰背景、白色文字。
 *
 * @param button 控件实例。
 * @param area   屏幕区域（闭区间）。
 * @param text   按钮文本；可为 NULL。
 */
void LEANGL_Button_Init(LEANGL_Button_t *button, const LEANGL_Rect_t *area,
                        const char *text);

/** @brief 更换文本并标脏。 */
void LEANGL_Button_Set_Text(LEANGL_Button_t *button, const char *text);

/** @brief 更换三种颜色并标脏。 */
void LEANGL_Button_Set_Colors(LEANGL_Button_t *button, LEANGL_Color_t bg,
                              LEANGL_Color_t pressed_bg,
                              LEANGL_Color_t text_color);

/** @brief 设置点击回调（松开时触发）。 */
void LEANGL_Button_On_Click(LEANGL_Button_t *button, LEANGL_Button_Cb callback);

/**
 * @brief 按下按钮。
 *
 * 只在“普通”状态下生效：进入按下状态并标脏。重复调用会被忽略。
 */
void LEANGL_Button_Press(LEANGL_Button_t *button);

/**
 * @brief 在按钮上松开。
 *
 * 只在“按下”状态下生效：回到普通状态、标脏，并触发 on_click。
 * 未按下时调用不产生任何效果。
 */
void LEANGL_Button_Release(LEANGL_Button_t *button);

/**
 * @brief 取消按下，不触发点击。
 *
 * 只在“按下”状态下生效：回到普通状态并标脏。用于手指移出按钮、
 * 触摸被系统取消等场景。
 */
void LEANGL_Button_Cancel(LEANGL_Button_t *button);

/** @brief 查询当前是否处于按下状态。 */
uint8_t LEANGL_Button_Is_Pressed(const LEANGL_Button_t *button);

#endif
