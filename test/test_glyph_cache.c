/**
 * @file    test_glyph_cache.c
 * @brief   字形缓存的主机端测试。
 *
 * 验证 LEANGL_GLYPH_CACHE_SIZE 生效后，重复测量/重复绘制同一批字符不会
 * 反复调用 glyph_cb：
 *   1. 同一段文本第二次测量宽度时回调次数不再增加；
 *   2. 分块绘制命中缓存，绘制阶段不产生任何回调；
 *   3. 重复绘制同样不增加回调；
 *   4. 不存在的字符只查询一次（负缓存）；
 *   5. 重新注册字体后缓存失效，回调重新发生。
 *
 * 编译（主机）：
 *   gcc -std=gnu99 -Wall -Wextra -DLEANGL_FLUSH_ASYNC=0 -Iinclude -I.
 *       test/test_glyph_cache.c <library sources> -o test/test_glyph_cache
 */

#include "leangl.h"
#include "leangl_internal.h"
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define SCREEN_W 64u
#define SCREEN_H 32u

static uint8_t s_fb[SCREEN_W * SCREEN_H * LEANGL_COLOR_BYTES];
static int s_cb_calls = 0;
static int s_failures = 0;

/* ------------------------------------------------------------------ */
/* 控制台编码                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief 让 Windows 控制台按 UTF-8 解释输出字节。
 *
 * @return 1 表示成功（或非 Windows 平台）；0 表示切换失败。
 */
static int test_console_utf8(void) {
#if defined(_WIN32)
    if (SetConsoleOutputCP(CP_UTF8) == 0)
        return 0;
    (void)SetConsoleCP(CP_UTF8);
    return 1;
#else
    return 1;
#endif
}

/** @brief 1 表示控制台支持 UTF-8，测试文案可用中文。 */
static int g_utf8_ok = 1;

/** @brief 按控制台能力选择中文或英文文案，避免输出乱码。 */
static const char *t(const char *utf8, const char *ascii) {
    return g_utf8_ok ? utf8 : ascii;
}

/* ------------------------------------------------------------------ */
/* 假硬件与字体                                                        */
/* ------------------------------------------------------------------ */

static void fake_flush(uint16_t y, uint16_t h, const uint8_t *data) {
    (void)y;
    (void)h;
    (void)data;
}

/**
 * @brief 计数取模回调。
 *
 * 每个字符都按 8x16 返回一块全亮的点阵，既方便检查像素，也让回调次数
 * 成为唯一被观察的指标。
 */
static uint8_t counting_glyph(const LEANGL_Font_t *font, LEANGL_CodePoint_t ch,
                              LEANGL_Glyph_t *out) {
    (void)font;
    s_cb_calls++;

    if (ch == (LEANGL_CodePoint_t)'?')
        return 0; /* 模拟字库里没有的字符 */

    out->size.w = 8;
    out->size.h = 16;
    memset(out->bits, 0xFF, 16u); /* (8 + 7) / 8 * 16 字节 */

    return 1;
}

static const LEANGL_Font_t s_font_a = LEANGL_FONT_INIT(16, counting_glyph, NULL);
static const LEANGL_Font_t s_font_b = LEANGL_FONT_INIT(16, counting_glyph, NULL);

/* ------------------------------------------------------------------ */
/* 控件                                                                */
/* ------------------------------------------------------------------ */

static void text_draw(LEANGL_Widget_t *w) {
    LEANGL_Text_Line_t opt;

    (void)w;

    opt.x = 0;
    opt.y = 0;
    opt.clip_x1 = 0;
    opt.clip_x2 = (uint16_t)(SCREEN_W - 1u);
    opt.ellipsis = 0;

    LEANGL_Set_Char_Color(LEANGL_COLOR_WHITE);
    (void)LEANGL_Draw_Text_Line("ABCA", &opt);
}

static LEANGL_Widget_t s_widget;

static void reset_mock(void) {
    s_cb_calls = 0;
    memset(s_fb, 0, sizeof s_fb);
    LEANGL_Set_Font(&s_font_a);
}

static void check(int cond, const char *utf8, const char *ascii) {
    printf("[%s] %s\n", cond ? " OK " : "FAIL", t(utf8, ascii));
    if (!cond)
        s_failures++;
}

/* ------------------------------------------------------------------ */

int main(void) {
    LEANGL_Rect_t full = {0, 0, (uint16_t)(SCREEN_W - 1u),
                          (uint16_t)(SCREEN_H - 1u)};
    uint8_t expected[LEANGL_COLOR_BYTES];

    /* 1. 初始化控制台 UTF-8 支持 */
    g_utf8_ok = test_console_utf8();

    LEANGL_Set_Screen(SCREEN_W, SCREEN_H);
    LEANGL_Set_Flush_CB(fake_flush);
    LEANGL_Buffer_Set(s_fb, sizeof s_fb, 0);

    LEANGL_Widget_Init(&s_widget, &full, text_draw, NULL);
    LEANGL_Widget_Add(&s_widget);

#if LEANGL_GLYPH_CACHE_SIZE == 0
    /* 2. 普通信息输出，使用 t() 宏 */
    printf("%s\n", t("LEANGL_GLYPH_CACHE_SIZE == 0，缓存已关闭，跳过测试",
                     "LEANGL_GLYPH_CACHE_SIZE == 0, cache disabled, skipping test"));
    return 0;
#else
    /* --- 1. 重复测量不重复取模 --- */
    reset_mock();
    (void)LEANGL_Font_Text_Width("ABC");
    check(s_cb_calls == 3, "首次测量 3 个不同字符 -> 3 次回调",
          "first measure of 3 unique chars -> 3 callbacks");
    (void)LEANGL_Font_Text_Width("ABC");
    (void)LEANGL_Font_Text_Width("ABC");
    check(s_cb_calls == 3, "再次测量同一文本 -> 回调次数不变",
          "re-measure same text -> callback count unchanged");

    /* --- 2. 绘制阶段全部命中缓存 --- */
    LEANGL_Widget_Invalidate(&s_widget);
    LEANGL_Flush();
    check(s_cb_calls == 3, "绘制已缓存字符 -> 不产生新的回调",
          "draw cached chars -> no new callbacks");

    /* --- 3. 重复绘制仍不增加回调 --- */
    LEANGL_Widget_Invalidate(&s_widget);
    LEANGL_Flush();
    check(s_cb_calls == 3, "二次重绘 -> 回调次数仍不变",
          "second redraw -> callback count still unchanged");

    /* 确认绘制真的落到了缓冲区（全亮字模的第 0 个像素）。 */
    LEANGL_RGB565_To_Bytes(expected, (uint16_t)LEANGL_COLOR_WHITE);
    check(s_fb[0] == expected[0] && s_fb[1] == expected[1],
          "文本确实写入了缓冲区",
          "text is actually written to the frame buffer");

    /* --- 4. 缺失字符的负缓存 --- */
    reset_mock();
    check(LEANGL_Font_Glyph((LEANGL_CodePoint_t)'?') == NULL,
          "缺失字符返回 NULL",
          "missing char returns NULL");
    check(s_cb_calls == 1, "缺失字符首次查询 -> 1 次回调",
          "missing char first query -> 1 callback");
    check(LEANGL_Font_Glyph((LEANGL_CodePoint_t)'?') == NULL,
          "缺失字符再次返回 NULL",
          "missing char returns NULL again");
    check(s_cb_calls == 1, "缺失字符被负缓存 -> 不再回调",
          "missing char is negatively cached -> no more callbacks");

    /* --- 5. 换字体使缓存失效 --- */
    LEANGL_Set_Font(&s_font_b);
    (void)LEANGL_Font_Text_Width("AB");
    check(s_cb_calls == 3, "换字体后重新取模 -> 回调重新发生",
          "changing font invalidates cache -> callbacks occur again");

    /* 3. 最终结果汇总输出 */
    printf("\n%s (%d %s)\n",
           s_failures ? t("存在失败", "FAILED") : t("全部通过", "ALL PASSED"),
           s_failures,
           s_failures ? t("项失败", "failure(s)") : "");
           
    return s_failures ? 1 : 0;
#endif
}