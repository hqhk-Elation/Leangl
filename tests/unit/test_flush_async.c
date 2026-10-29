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
 * @file    test_flush_async.c
 * @brief   LEANGL_Flush() 分块刷新的主机端模拟测试。
 *
 * 用假的 port 层在 PC 上跑 LEANGL_Handler() 循环，验证：
 *   1. 异步（DMA）模式下，即使缓冲一直处于“发送中”，整屏每条像素带最终
 *      都能被发送出去（修复前的饥饿问题）；
 *   2. 已提交的块不会被重复绘制/重复发送；
 *   3. 最后一块不足 buffer.lines 时行数正确；
 *   4. 脏区清空后不再调用 flush_cb。
 *
 * 编译（主机）：
 *   cmake --preset host && cmake --build --preset host
 *   ctest --preset host -R flush_async --output-on-failure
 */

#include "leangl.h"
#include "leangl_internal.h"
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* ------------------------------------------------------------------ */
/* 控制台编码                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief 让控制台按 UTF-8 解释输出字节。
 *
 * 本文件的字符串字面量是 UTF-8，而 Windows 控制台默认代码页通常是 936(GBK)，
 * 不切换的话中文会被按双字节错误解析（“屏幕” → “灞忓箷”）。
 * 输出被重定向到文件时该调用无副作用。非 Windows 平台无需处理。
 *
 * @return 1 表示控制台已是 UTF-8；0 表示切换失败（调用方应改用纯 ASCII 文案）。
 */
static int test_console_utf8(void) {
#if defined(_WIN32)
    if (SetConsoleOutputCP(CP_UTF8) == 0)
        return 0;
    (void)SetConsoleCP(CP_UTF8); /* 输入代码页一并切换；失败不影响输出 */
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
/* 假硬件                                                              */
/* ------------------------------------------------------------------ */

#define SCREEN_W 320u
#define SCREEN_H 230u /* 故意不是 buffer.lines 的整数倍 */
#define BAND_LINES 40u

static uint8_t s_fb[SCREEN_W * BAND_LINES * 2 * 2]; /* 双缓冲 */
static uint8_t s_panel[SCREEN_W * SCREEN_H];        /* 记录哪些行被发过 */

static int s_send_y[64];
static int s_send_h[64];
static int s_send_count = 0;
static int s_in_flight = 0;

static void fake_flush(uint16_t y, uint16_t h, const uint8_t *data) {
    (void)data;
    if (s_send_count < 64) {
        s_send_y[s_send_count] = (int)y;
        s_send_h[s_send_count] = (int)h;
        s_send_count++;
    }
    /* 模拟 DMA：只启动传输，不在这里完成。 */
    s_in_flight++;
    memset(&s_panel[(size_t)y * SCREEN_W], 1, (size_t)h * SCREEN_W);
}

/* 满屏背景控件：保证每一块都被完整填充 */
static void bg_draw(LEANGL_Widget_t *w) {
    LEANGL_Fill_Rect(&w->area, LEANGL_COLOR_BLACK);
}

static LEANGL_Widget_t s_bg;

static void reset_mock(void) {
    s_send_count = 0;
    s_in_flight = 0;
    memset(s_panel, 0, sizeof s_panel);
}

/* 模拟一次 DMA 传输完成 */
static void dma_complete(void) {
    if (s_in_flight > 0) {
        s_in_flight--;
        LEANGL_Buf_Ready();
    }
}

static int failures = 0;

/**
 * @brief 记录一项检查结果。
 *
 * @param cond  条件为真表示通过。
 * @param utf8  控制台支持 UTF-8 时使用的描述。
 * @param ascii 控制台不支持 UTF-8 时的英文描述。
 */
static void check(int cond, const char *utf8, const char *ascii) {
    printf("[%s] %s\n", cond ? " OK " : "FAIL", t(utf8, ascii));
    if (!cond)
        failures++;
}

/* ------------------------------------------------------------------ */

int main(void) {
    LEANGL_Rect_t full = {0, 0, (uint16_t)(SCREEN_W - 1), (uint16_t)(SCREEN_H - 1)};

    g_utf8_ok = test_console_utf8();

    LEANGL_Set_Screen(SCREEN_W, SCREEN_H);
    LEANGL_Set_Flush_CB(fake_flush);
    LEANGL_Buffer_Set(s_fb, sizeof s_fb, 1);

    LEANGL_Widget_Init(&s_bg, &full, bg_draw, 0);
    LEANGL_Widget_Add(&s_bg);
    LEANGL_Widget_Invalidate(&s_bg);

    printf(t("buffer.lines = %u, 屏幕 %u 行, 共 %u 条像素带\n",
             "buffer.lines = %u, screen %u rows, %u bands\n"),
           (unsigned)buffer.lines, (unsigned)SCREEN_H,
           (unsigned)((SCREEN_H + buffer.lines - 1) / buffer.lines));

    /* --- 场景 1：异步模式下逐轮推进，整屏最终刷完 --- */
    reset_mock();
    {
        int round;
        for (round = 0; round < 40 && LEANGL_Dirty_Count() > 0; round++) {
            LEANGL_Handler();
            dma_complete(); /* 每轮恰好完成一次传输，模拟单通道 DMA */
        }
    }

    /* 每条像素带的起始行都应当发送过一次 */
    {
        int y, all = 1;
        for (y = 0; y < (int)SCREEN_H; y += (int)buffer.lines) {
            int found = 0, i;
            for (i = 0; i < s_send_count; i++)
                if (s_send_y[i] == y)
                    found = 1;
            if (!found) {
                printf(t("      缺失像素带 y=%d\n", "      missing band y=%d\n"), y);
                all = 0;
            }
        }
        check(all, "所有像素带都被发送（无饥饿）", "every band was sent (no starvation)");
    }

    /* 没有重复发送 */
    {
        int i, dup = 0, j;
        for (i = 0; i < s_send_count; i++)
            for (j = i + 1; j < s_send_count; j++)
                if (s_send_y[i] == s_send_y[j])
                    dup = 1;
        check(!dup, "已提交的块没有被重复发送", "committed bands are not sent twice");
    }

    /* 最后一块行数正确 */
    {
        int expect = (int)(SCREEN_H % buffer.lines);
        int i, last = -1;
        if (expect == 0)
            expect = (int)buffer.lines;
        for (i = 0; i < s_send_count; i++)
            if (s_send_y[i] + s_send_h[i] == (int)SCREEN_H)
                last = s_send_h[i];
        check(last == expect, "最后一条像素带行数被截断正确",
              "last band height is clamped correctly");
    }

    check(LEANGL_Dirty_Is_Empty(), "刷新完成后脏区已清空",
          "dirty list is empty after flush");

    /* --- 场景 2：无脏区时不得调用 flush_cb --- */
    reset_mock();
    LEANGL_Handler();
    LEANGL_Handler();
    check(s_send_count == 0, "无脏区时不调用 flush_cb",
          "flush_cb is not called without dirty area");

    /* --- 场景 3：局部脏区只刷对应像素带 --- */
    reset_mock();
    {
        LEANGL_Rect_t small = {10, 100, 20, 110}; /* 落在第 2 条带（80..119） */
        LEANGL_Dirty_Add(small.x1, small.y1, small.x2, small.y2);
    }
    {
        int round;
        for (round = 0; round < 10 && LEANGL_Dirty_Count() > 0; round++) {
            LEANGL_Handler();
            dma_complete();
        }
    }
    check(s_send_count == 1 && s_send_y[0] == 80,
          "局部脏区只刷新所在的一条像素带",
          "a small dirty rect only flushes its own band");

    /* --- 场景 4：脏区跨块时逐块清除 --- */
    reset_mock();
    LEANGL_Dirty_Add(0, 0, (uint16_t)(SCREEN_W - 1), 150);
    {
        int round;
        for (round = 0; round < 20 && LEANGL_Dirty_Count() > 0; round++) {
            LEANGL_Handler();
            dma_complete();
        }
    }
    check(s_send_count == 4, "跨 4 条像素带的脏区全部刷完",
          "a dirty rect spanning 4 bands flushes all of them");
    check(LEANGL_Dirty_Is_Empty(), "跨块脏区被逐块清空",
          "bands are cleared one by one as they commit");

    printf(t("\n%s (%d 项失败)\n", "\n%s (%d failure(s))\n"),
           failures ? t("存在失败", "FAILED") : t("全部通过", "ALL PASSED"),
           failures);
    return failures ? 1 : 0;
}
