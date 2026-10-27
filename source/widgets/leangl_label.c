#include "leangl_label.h"
#include "leangl_colors.h"
#include "leangl_draw_char.h"
#include "leangl_draw_shape.h"
#include "leangl_font.h"
#include "leangl_types.h"
#include <stddef.h>
#include <stdint.h>

static void label_draw(LEANGL_Widget_t *w) {
    LEANGL_Label_t *label = (LEANGL_Label_t *)w;
    uint16_t area_w;
    uint16_t line_h;
    uint16_t x;
    uint16_t y;
    const char *p;

    if (label->text == NULL)
        return;

    /* 不透明时先用背景色铺满整个控件区域，避免文本变短后留下残影。 */
    if (!label->is_transparent)
        LEANGL_Fill_Rect(&label->base.area, label->bg_color);

    LEANGL_Set_Char_Color(label->text_color);

    /* 区域非法（宽或高为 0）时直接返回，后面的取模与绘制都省掉。 */
    if (w->area.x2 < w->area.x1 || w->area.y2 < w->area.y1)
        return;

    area_w = (uint16_t)(w->area.x2 - w->area.x1 + 1);

    /* 行高取字体标称高度，使同一 Label 内各行等距，
       不受个别字符实际高度的影响。字体未注册时返回 0，
       此时后续 Char_Size 也必然为 0，无需再往下走。 */
    line_h = LEANGL_Font_Height();
    if (line_h == 0)
        return;

    x = w->area.x1;
    y = w->area.y1;

    /* 按 Unicode 码点遍历：一个 UTF-8 中文字符只绘制一次，
       不会像逐字节那样被拆成 3 个乱码字。 */
    p = label->text;
    while (*p != '\0') {
        LEANGL_CodePoint_t cp = LEANGL_Utf8_Next(&p);
        LEANGL_Font_Size_t size;

        if (cp == LEANGL_CODEPOINT_NONE)
            continue; /* 非法字节序列已由解码器跳过 */

        if (cp == '\r')
            continue; /* CRLF 的 \r 直接忽略，只认 \n */

        if (cp == '\n') {
            /* 显式换行。注意 Utf8_Next 已经把 p 推过 '\n'，
               不能再额外跳过字节，否则会吃掉下一行的首字符。 */
            x = w->area.x1;
            y = (uint16_t)(y + line_h);
            if (y > w->area.y2)
                break;
            continue;
        }

        size = LEANGL_Char_Size(cp);
        if (size.w == 0)
            continue; /* 无此字：跳过它，但不中断后面文字的绘制 */

        /* 单字本身就比整行还宽：画出来必然越界，跳过以免反复换行。 */
        if (size.w > area_w)
            continue;

        /* 先判断放不放得下，再决定要不要换行。
           顺序不能颠倒——先画再判断会让这个字画出右边界。 */
        if ((uint32_t)x + size.w - 1u > w->area.x2) {
            x = w->area.x1;
            y = (uint16_t)(y + line_h);
            if (y > w->area.y2)
                break;
        }

        /* 整行在垂直方向放不下：结束（闭区间，y + line_h - 1 才是末行）。 */
        if ((uint32_t)y + line_h - 1u > w->area.y2)
            break;

        (void)LEANGL_Draw_Char_Ex(x, y, cp);
        x = (uint16_t)(x + size.w);
    }
}

void LEANGL_Label_Init(LEANGL_Label_t *label, const LEANGL_Rect_t *area,
                       const char *text) {
    if (label == NULL)
        return;

    LEANGL_Widget_Init(&label->base, area, label_draw, NULL);
    LEANGL_Label_Set_Bg_Color(label, LEANGL_COLOR_WHITE);
    LEANGL_Label_Set_Transparent(label, LEANGL_FALSE);
    label->text = NULL;
    label->text_color = LEANGL_COLOR_BLACK;
    LEANGL_Label_Set_Text(label, text);
}

void LEANGL_Label_Set_Text(LEANGL_Label_t *label, const char *text) {
    if (label == NULL)
        return;

    label->text = text;
    LEANGL_Widget_Invalidate(&label->base);
}

void LEANGL_Label_Set_Text_Color(LEANGL_Label_t *label, LEANGL_Color_t color) {
    if (label == NULL)
        return;

    label->text_color = color;
    LEANGL_Widget_Invalidate(&label->base);
}

void LEANGL_Label_Set_Bg_Color(LEANGL_Label_t *label, LEANGL_Color_t color) {
    if (label == NULL)
        return;

    label->bg_color = color;
    LEANGL_Widget_Invalidate(&label->base);
}

void LEANGL_Label_Set_Transparent(LEANGL_Label_t *label,
                                  uint8_t is_transparent) {
    if (label == NULL)
        return;

    label->is_transparent = is_transparent;
    LEANGL_Widget_Invalidate(&label->base);
}
