#include "leangl_draw_char.h"
#include "leangl_buffer.h"
#include "leangl_draw_pixel.h"
#include "leangl_flush.h"
#include "leangl_font.h"
#include "leangl_internal.h"
#include "leangl_port.h"
#include <stddef.h>
#include <stdint.h>

/* 模块级前景色。控件在绘制前都会通过 LEANGL_Set_Char_Color() 覆盖它，
   因此这里只需要一个确定的初值，不能让读取未初始化内存。 */
static LEANGL_Color_t char_col = 0xff;

void LEANGL_Set_Char_Color(LEANGL_Color_t color) {
    char_col = color;
}

LEANGL_Font_Size_t LEANGL_Char_Size(LEANGL_CodePoint_t ch) {
    LEANGL_Font_Size_t size = {0, 0};
    const LEANGL_Glyph_t *glyph = LEANGL_Font_Glyph(ch);

    if (glyph != NULL)
        size = glyph->size;

    return size;
}

/**
 * @brief 一次绘制过程中保持不变的上下文。
 *
 * 逐字符（甚至逐像素）重复读取 leangl_flush、调用 LEANGL_Get_Buf()、
 * 重新计算屏幕宽度，会把这些访问放大几十倍。绘制前算一次，之后只传指针。
 */
typedef struct {
    uint8_t *buf;       /**< 当前活动缓冲区首地址。 */
    uint16_t screen_w;  /**< 屏幕宽度（像素）。 */
    int band_top;       /**< 当前像素带首行（屏幕坐标）。 */
    int band_bottom;    /**< 当前像素带末行 + 1（屏幕坐标，开区间）。 */
    LEANGL_Color_t color; /**< 前景色。 */
} LEANGL_Draw_Ctx_t;

/**
 * @brief 收集当前刷新窗口信息。
 *
 * @return 1 可以绘制；0 当前没有刷新窗口或缓冲区未就绪，调用方应跳过绘制。
 */
static uint8_t draw_ctx_init(LEANGL_Draw_Ctx_t *ctx) {
    int rows = (int)leangl_flush.rows;

    if (rows <= 0) /* 没有正在刷新的像素带 */
        return 0;

    ctx->buf = LEANGL_Get_Buf();
    if (ctx->buf == NULL) /* 缓冲区还没初始化 */
        return 0;

    ctx->screen_w = (uint16_t)leangl_port.screen.width;
    if (ctx->screen_w == 0)
        return 0;

    ctx->band_top = (int)leangl_flush.y0;
    ctx->band_bottom = ctx->band_top + rows;
    ctx->color = char_col;

    return 1;
}

/**
 * @brief 把一个已取出的字模画到缓冲区。
 *
 * 垂直/水平裁剪都提前折算成循环起止下标，循环里不再逐像素判断越界。
 * 每行只算一次目标地址，之后按步长推进。
 *
 * @param ctx   绘制上下文（缓冲区、像素带、颜色）。
 * @param glyph 字模；尺寸须已通过 LEANGL_Font_Glyph() 的校验。
 * @param x     字符左上角横坐标（可为负，用于滚动时从左侧进入）。
 * @param y     字符左上角纵坐标（屏幕坐标）。
 */
static void draw_glyph(const LEANGL_Draw_Ctx_t *ctx,
                       const LEANGL_Glyph_t *glyph, int x, int y) {
    const int w = (int)glyph->size.w;
    const int h = (int)glyph->size.h;
    const int bytes_per_row = (w + 7) / 8;
    int row_begin = 0;
    int row_end = h;
    int col_begin = 0;
    int col_end = w;
    int row;

    /* 垂直裁剪：分块刷新时一个字符会被切成几段画。 */
    if (y < ctx->band_top)
        row_begin = ctx->band_top - y;
    if (y + h > ctx->band_bottom)
        row_end = ctx->band_bottom - y;
    if (row_begin >= row_end)
        return;

    /* 水平裁剪：屏幕外的列直接跳过。 */
    if (x < 0)
        col_begin = -x;
    if (x + w > (int)ctx->screen_w)
        col_end = (int)ctx->screen_w - x;
    if (col_begin >= col_end)
        return;

    for (row = row_begin; row < row_end; row++) {
        const uint8_t *src = glyph->bits + (uint32_t)row * bytes_per_row;
        uint8_t *dst = ctx->buf +
                       ((uint32_t)(y + row - ctx->band_top) * ctx->screen_w +
                        (uint32_t)(x + col_begin)) *
                           LEANGL_COLOR_BYTES;
        int col;

        for (col = col_begin; col < col_end; col++) {
            /* MSB-first：bit7 是最左边的像素。 */
            if (src[col >> 3] & (uint8_t)(0x80u >> (col & 0x07)))
                LEANGL_Pixel_Write(dst, ctx->color);
            dst += LEANGL_COLOR_BYTES;
        }
    }
}

LEANGL_Font_Size_t LEANGL_Draw_Char_Ex(uint16_t x, uint16_t y,
                                       LEANGL_CodePoint_t ch) {
    LEANGL_Font_Size_t size = {0, 0};
    LEANGL_Draw_Ctx_t ctx;
    const LEANGL_Glyph_t *glyph;

    /* 字模统一由字体模块取（带缓存），尺寸校验也在那里做完。 */
    glyph = LEANGL_Font_Glyph(ch);
    if (glyph == NULL)
        return size;

    size = glyph->size;

    if (draw_ctx_init(&ctx))
        draw_glyph(&ctx, glyph, (int)x, (int)y);

    return size;
}

void LEANGL_Draw_Char(uint16_t x, uint16_t y, LEANGL_CodePoint_t ch) {
    (void)LEANGL_Draw_Char_Ex(x, y, ch);
}

/* ------------------------------------------------------------------ */
/* 单行文本                                                            */
/* ------------------------------------------------------------------ */

/** @brief 省略号使用的字符。 */
#define ELLIPSIS_CHAR '.'

/** @brief 省略号占用的字符数。 */
#define ELLIPSIS_COUNT 3u

/** @brief 取 '.' 的宽度；字体里没有 '.' 时返回 0（表示不支持省略号）。 */
static uint16_t ellipsis_dot_width(void) {
    return LEANGL_Char_Size((LEANGL_CodePoint_t)ELLIPSIS_CHAR).w;
}

/**
 * @brief 计算一行文本（到 '\n' 或结尾）的总宽度。
 *
 * 用 uint32 累加，避免长文本在 uint16 上提前回绕。
 */
static uint32_t line_advance(const char *text) {
    const char *p = text;
    uint32_t total = 0;

    while (*p != '\0') {
        LEANGL_CodePoint_t cp = LEANGL_Utf8_Next(&p);
        LEANGL_Font_Size_t size;

        if (cp == LEANGL_CODEPOINT_NONE)
            continue; /* 非法序列，解码器已跳过 */
        if (cp == '\n')
            break; /* 单行：到此为止 */
        if (cp == '\r')
            continue;

        size = LEANGL_Char_Size(cp);
        if (size.w == 0)
            continue; /* 无此字，不占宽度 */

        total += size.w;
    }

    return total;
}

uint16_t LEANGL_Draw_Text_Line(const char *text, const LEANGL_Text_Line_t *opt) {
    int32_t x;
    int32_t stop_x;      /* 不允许越过的右边界（已扣除省略号预留） */
    uint32_t total;
    uint16_t dot_w = 0;
    uint8_t use_dots = 0;
    uint8_t can_draw;
    LEANGL_Draw_Ctx_t ctx;
    const char *p;

    if (text == NULL || opt == NULL)
        return 0;
    if (opt->clip_x2 < opt->clip_x1)
        return 0;

    total = line_advance(text);
    if (total > UINT16_MAX)
        total = UINT16_MAX;

    /* 可用宽度（闭区间）。 */
    {
        uint32_t avail = (uint32_t)opt->clip_x2 - opt->clip_x1 + 1u;

        if (opt->ellipsis && (uint32_t)total > avail) {
            dot_w = ellipsis_dot_width();
            /* 连三个点都放不下时退化为纯裁剪，免得画出一串残缺的点。 */
            if (dot_w != 0 && (uint32_t)dot_w * ELLIPSIS_COUNT < avail)
                use_dots = 1;
        }
    }

    stop_x = (int32_t)opt->clip_x2;
    if (use_dots)
        stop_x -= (int32_t)dot_w * (int32_t)ELLIPSIS_COUNT;

    /* 缓冲区、屏幕宽度、像素带都只在这里取一次，整行共用。 */
    can_draw = draw_ctx_init(&ctx);

    x = opt->x;

    /* 主循环：放不下就停，交给后面的省略号。 */
    p = text;
    while (*p != '\0') {
        LEANGL_CodePoint_t cp = LEANGL_Utf8_Next(&p);
        const LEANGL_Glyph_t *glyph;
        int32_t w;

        if (cp == LEANGL_CODEPOINT_NONE)
            continue;
        if (cp == '\n')
            break;
        if (cp == '\r')
            continue;

        /* 只取一次字模：尺寸与点阵都从这里来，不再"先 Char_Size 再画"。
           配合字体层的缓存，测量阶段已取过的字在这里直接命中。 */
        glyph = LEANGL_Font_Glyph(cp);
        if (glyph == NULL)
            continue; /* 无此字，不占宽度 */

        w = (int32_t)glyph->size.w;

        /* 这个字符会越过右边界：停止（若启用省略号，下面补 "..."）。 */
        if (x + w - 1 > stop_x)
            break;

        /* 完整落在裁剪窗口内才画：左侧移出的部分直接跳过，仍然推进 x。 */
        if (can_draw && x >= (int32_t)opt->clip_x1)
            draw_glyph(&ctx, glyph, (int)x, (int)opt->y);

        x += w;
    }

    if (use_dots) {
        const LEANGL_Glyph_t *dot =
            LEANGL_Font_Glyph((LEANGL_CodePoint_t)ELLIPSIS_CHAR);
        unsigned int i;

        if (dot != NULL) {
            for (i = 0; i < ELLIPSIS_COUNT; i++) {
                if (x + (int32_t)dot_w - 1 > (int32_t)opt->clip_x2)
                    break;
                if (can_draw && x >= (int32_t)opt->clip_x1)
                    draw_glyph(&ctx, dot, (int)x, (int)opt->y);
                x += (int32_t)dot_w;
            }
        }
    }

    return (uint16_t)total;
}
