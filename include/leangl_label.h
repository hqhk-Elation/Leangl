#ifndef LEANGL_LABEL_H
#define LEANGL_LABEL_H

#include "leangl_colors.h"
#include "leangl_widget.h"
#include <stdint.h>

/**
 * @file    leangl_label.h
 * @brief   文本标签控件。
 *
 * 文本为 UTF-8 编码的多行字符串，库负责解码，一个中文只绘制一次。
 * 文本指针由调用方持有，控件只保存引用，不拷贝内容。
 *
 * 排版规则：
 *   - 遇到 '\n' 显式换行（'\r' 被忽略，因此 CRLF 也能正常工作）；
 *   - 单行放不下时自动折行（按字符折，不拆分单词）；
 *   - 行高取字体的标称高度（LEANGL_Font_Height），保证各行等距；
 *   - 超出区域右边界的字不会被绘制；整行放不下时停止绘制；
 *   - 字符在字库中不存在时跳过该字符，继续绘制后面的内容。
 */

/** @brief 文本标签控件。 */
typedef struct {
    LEANGL_Widget_t base;      /**< 基类，必须是第一个成员 */
    const char *text;          /**< UTF-8 文本指针，生命周期由调用方保证 */
    LEANGL_Color_t text_color; /**< 文字颜色 */
    LEANGL_Color_t bg_color;   /**< 背景颜色，仅在 is_transparent 为 0 时生效 */
    uint8_t is_transparent;    /**< 非 0 表示不填背景，透出下层像素 */
} LEANGL_Label_t;

/**
 * @brief 初始化标签控件。
 *
 * 不会自动加入控件表，初始化后需调用 LEANGL_Widget_Add()。
 *
 * @param label 控件实例。
 * @param area  屏幕区域。
 * @param text  文本；可为 NULL。
 */
void LEANGL_Label_Init(LEANGL_Label_t *label, const LEANGL_Rect_t *area,
                       const char *text);

/** @brief 更换文本并标脏。 */
void LEANGL_Label_Set_Text(LEANGL_Label_t *label, const char *text);

/** @brief 更换文字颜色并标脏。 */
void LEANGL_Label_Set_Text_Color(LEANGL_Label_t *label, LEANGL_Color_t color);

/** @brief 更换背景颜色并标脏。 */
void LEANGL_Label_Set_Bg_Color(LEANGL_Label_t *label, LEANGL_Color_t color);

/** @brief 设置背景是否透明并标脏。
 *
 * @param is_transparent 非 0 表示不填背景色（透出下层），0 表示用 bg_color 填满整个区域。
 */
void LEANGL_Label_Set_Transparent(LEANGL_Label_t *label,
                                  uint8_t is_transparent);

#endif
