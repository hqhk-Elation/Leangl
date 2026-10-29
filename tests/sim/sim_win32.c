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
 * @file    sim_win32.c
 * @brief   LeanGL 的 Win32 屏幕模拟器引擎（窗口、显存、字库、刷新调度、耗时模型）。
 *
 * 目的：在没有目标板的情况下用一个 Win32 窗口模拟一块 TFT 屏，并尽量还原
 * MCU 上的时间特性，让“分块刷新”、“字形缓存”、“滚动动画”这类问题在 PC 上
 * 就能暴露出来。
 *
 * 与具体界面解耦：界面由 leangl_sim_ui() 提供（见 sim.h），演示界面在
 * sim_demo.c。换一套界面只需替换那个文件，本文件不用动。
 *
 * 模拟的硬件特性（全部可在 sim_config.ini 里调，单位微秒）：
 *   - flash_latency_us ：每次取字模的耗时，模拟字库放在外部 SPI Flash；
 *   - raster_latency_us：字形光栅化的耗时（GDI 回退路径的缓存未命中才付）；
 *   - flush_latency_us ：每块像素带传屏的耗时，模拟 SPI/DMA 传输；
 *   - busy_wait        ：1 = 忙等（裸机 CPU），0 = Sleep（让出 CPU）。
 *
 * 支持两种刷新模式，由编译期的 LEANGL_FLUSH_ASYNC 决定：
 *   - 阻塞式：flush_cb 返回前就把像素写进窗口显存；
 *   - 异步式：flush_cb 只把像素拷进“DMA 缓冲”并立即返回，主循环模拟传输完成
 *     中断，按发起顺序调用 LEANGL_Buf_Ready()。
 *
 * 用法：
 *   leangl_sim [选项]
 *     --config PATH      配置文件；不指定时依次找 sim_config.ini、
 *                        sim/sim_config.ini、tests/sim/sim_config.ini
 *     --size WxH         屏幕分辨率（覆盖配置文件）
 *     --band-lines N     每条像素带的行数，0 表示按高度自动取 1/8
 *     --flush-latency US 每块像素带传屏的模拟耗时（覆盖配置文件）
 *     --no-delay         把所有模拟延迟清零，用主机速度跑
 *     --font PATH        字库 .hex 路径（覆盖配置文件）
 *     --verbose          打印配置与逐块刷新日志
 *     --selftest [毫秒]  不开窗口跑到画面稳定后退出，全部像素带刷到则返回 0
 *     --dump out.bmp     把当前画面存成 BMP（可与 --selftest 叠加）
 *
 * 字库来源有两种：
 *   1. 找到 .hex 文件时用 unifont（见 sim_unifont.c），支持全部 CJK 与 ASCII，
 *      宽度由 Unifont 自己决定（8 或 16 像素）；
 *   2. 找不到时回退到 GDI 现场渲染（ASCII 8x16 + CJK 16x16），不依赖外部文件。
 *
 * 构建见 tests/CMakeLists.txt：
 *   cmake --preset host && cmake --build --preset host
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "leangl.h"
#include "sim.h"
#include "sim_config.h"
#include "sim_unifont.h"

/* ================================================================== */
/* 配置                                                                */
/* ================================================================== */

/** 模拟 DMA 的并发槽数：双缓冲下最多两块同时在飞。 */
#define SIM_DMA_SLOTS 2

/** 生效的配置：默认值 → 配置文件 → 命令行，后者覆盖前者。 */
static Sim_Config_t g_cfg;

/** 每条像素带的行数（配置里为 0 时按屏幕高度自动算出）。 */
static uint16_t g_band_lines = 0;

/* ---- 模拟耗时统计：自检结束时会打印，用来判断瓶颈在字库还是刷屏 ---- */
static uint64_t g_delay_flash_us = 0;
static uint64_t g_delay_raster_us = 0;
static uint64_t g_delay_flush_us = 0;
static uint32_t g_glyph_calls = 0;
static uint32_t g_raster_calls = 0;

/* ================================================================== */
/* 模拟时钟与耗时                                                      */
/* ================================================================== */

static LARGE_INTEGER g_qpc_freq;

static void sim_clock_init(void) {
    QueryPerformanceFrequency(&g_qpc_freq);
    if (g_qpc_freq.QuadPart <= 0)
        g_qpc_freq.QuadPart = 1;
}

/** @brief 单调微秒时钟，用作 tick 与 DMA 调度的时间基准。 */
static uint64_t sim_now_us(void) {
    LARGE_INTEGER c;

    QueryPerformanceCounter(&c);
    return (uint64_t)(c.QuadPart * 1000000LL / g_qpc_freq.QuadPart);
}

/**
 * @brief 模拟一段 MCU 上的耗时。
 *
 * busy_wait = 1 时死循环：裸机没有“睡眠”这种概念，这样也能顺带验证库
 * 不依赖操作系统的调度。为 0 时用 Sleep 让出 CPU，跑长时间测试更友好。
 */
static void sim_delay_us(uint32_t us) {
    if (us == 0)
        return;

    if (g_cfg.busy_wait) {
        uint64_t t0 = sim_now_us();

        while (sim_now_us() - t0 < (uint64_t)us) {
            /* 忙等 */
        }
    } else {
        Sleep((DWORD)((us + 999u) / 1000u));
    }
}

/* ================================================================== */
/* 运行期状态                                                          */
/* ================================================================== */

static HWND g_hwnd = NULL;
static HDC g_dib_dc = NULL;   /* 选入屏幕 DIB 的内存 DC */
static HBITMAP g_dib = NULL;
static uint32_t *g_pixels = NULL; /* DIB 位：32bpp，每像素 0xFFRRGGBB */

static volatile int g_need_paint = 0;
static volatile int g_quit = 0;
static int g_mouse_down = 0;

static uint32_t g_band_flushes = 0; /* flush_cb 被调用的次数 */
static uint8_t *g_band_seen = NULL; /* 每条像素带是否被发送过 */
static uint16_t g_band_count = 0;

/* ---- LeanGL 绘图缓冲区（运行时分配，支持任意分辨率） ---- */
static uint8_t *g_framebuf = NULL;
static uint32_t g_framebuf_bytes = 0;

/* ---- 字模渲染 ---- */
#define SIM_GLYPH_W 8       /* ASCII 字宽 */
#define SIM_GLYPH_WIDE_W 16 /* 全角（中文等）字宽 */
#define SIM_GLYPH_H 16      /* 两者等高 */

/* ASCII 字形：8x16，每行 1 字节 */
static HDC g_glyph_dc = NULL;
static HBITMAP g_glyph_bmp = NULL;
static void *g_glyph_bits = NULL;
static HFONT g_font = NULL;

typedef struct {
    uint8_t valid;
    uint8_t bits[SIM_GLYPH_H]; /* 每行 1 字节，MSB first */
} Sim_Glyph_t;

static Sim_Glyph_t g_glyph_cache[128];

/* 全角字形：16x16，每行 2 字节。用 GDI 以中文字体渲染，缓存 256 项。 */
static HDC g_wide_dc = NULL;
static HBITMAP g_wide_bmp = NULL;
static void *g_wide_bits = NULL;
static HFONT g_wide_font = NULL;

#define SIM_WIDE_CACHE 256

typedef struct {
    LEANGL_CodePoint_t codepoint; /* 0 表示这一格还空着 */
    uint8_t bits[SIM_GLYPH_H * 2];
} Sim_Wide_Glyph_t;

/* 直接映射缓存：index = codepoint % SIM_WIDE_CACHE。
   不同码点撞到同一格时会互相顶掉，但只是重新渲染一次，不影响正确性。 */
static Sim_Wide_Glyph_t g_wide_cache[SIM_WIDE_CACHE];

/* ---- DMA 模拟（仅异步模式） ---- */
#if LEANGL_FLUSH_ASYNC
typedef struct {
    uint16_t y;
    uint16_t h;
    uint64_t due_us;
    uint8_t busy;
} Sim_DmaSlot_t;

static Sim_DmaSlot_t g_dma_slot[SIM_DMA_SLOTS];
static uint8_t *g_dma_pool = NULL; /* SIM_DMA_SLOTS 份像素副本 */
static uint32_t g_dma_stride = 0;  /* 每份副本的字节数 */
static int g_dma_head = 0;         /* 最早发起的槽 */
static int g_dma_count = 0;        /* 在飞数量 */
#endif

/* ================================================================== */
/* 引擎信息（sim.h）                                                  */
/* ================================================================== */

uint16_t LEANGL_Sim_Width(void) {
    return g_cfg.width;
}

uint16_t LEANGL_Sim_Height(void) {
    return g_cfg.height;
}

uint16_t LEANGL_Sim_Band_Lines(void) {
    return g_band_lines;
}

uint16_t LEANGL_Sim_Band_Count(void) {
    return g_band_count;
}

uint32_t LEANGL_Sim_Band_Flushes(void) {
    return g_band_flushes;
}

const char *LEANGL_Sim_Mode(void) {
#if LEANGL_FLUSH_ASYNC
    return "async";
#else
    return "blocking";
#endif
}

/**
 * @brief 是否每条像素带都至少被送到过一次屏幕。
 *
 * 自检用它判断“整屏已经完整刷过一轮”。饥饿的典型现象就是某些带永远轮不到，
 * 因此这个条件同时也是一条回归防线。
 */
static int sim_all_bands_seen(void) {
    uint16_t i;

    if (g_band_seen == NULL || g_band_count == 0)
        return 0;

    for (i = 0; i < g_band_count; i++) {
        if (!g_band_seen[i])
            return 0;
    }

    return 1;
}

void LEANGL_Sim_Refresh(void) {
    LEANGL_Dirty_Add(0, 0, (uint16_t)(g_cfg.width - 1),
                     (uint16_t)(g_cfg.height - 1));
}

void LEANGL_Sim_Quit(void) {
    g_quit = 1;
}

/* ================================================================== */
/* 字模：用 GDI 渲染并缓存                                              */
/* ================================================================== */

static void sim_render_glyph(uint16_t ch, uint8_t *out) {
    uint8_t *px = (uint8_t *)g_glyph_bits;
    int y;

    /* 1bpp DIB 的内存首行是位图最下面一行，所以源行号要翻转。 */
    PatBlt(g_glyph_dc, 0, 0, SIM_GLYPH_W, SIM_GLYPH_H, BLACKNESS);

    if (ch >= 32 && ch < 127) {
        wchar_t wc = (wchar_t)ch;
        (void)TextOutW(g_glyph_dc, 0, 0, &wc, 1);
    }

    /* 读取 DIB 位之前必须刷新 GDI 批处理。 */
    GdiFlush();

    /* 1bpp、宽 8 -> 每行 1 字节，DIB 行按 4 字节对齐，故 stride = 4。 */
    for (y = 0; y < SIM_GLYPH_H; y++)
        out[y] = px[(SIM_GLYPH_H - 1 - y) * 4];
}

/**
 * @brief 渲染一个全角字形（16x16）到 out。
 *
 * out 需要 32 字节：每行 2 字节、共 16 行。
 * 中文字体由调用方保证已选入 g_wide_dc。
 */
static void sim_render_wide_glyph(LEANGL_CodePoint_t cp, uint8_t *out) {
    uint8_t *px = (uint8_t *)g_wide_bits;
    wchar_t wc;
    int y;
    int b;

    PatBlt(g_wide_dc, 0, 0, SIM_GLYPH_WIDE_W, SIM_GLYPH_H, BLACKNESS);

    /* 目标平台是 Windows，wchar_t 为 16 位（UTF-16），BMP 内码点可直接当宽字符。
       BMP 之外的码点（emoji 等）用代理对才能表示，这里不处理。 */
    if (cp > 0xFFFFu)
        return;

    wc = (wchar_t)cp;
    (void)TextOutW(g_wide_dc, 0, 0, &wc, 1);
    GdiFlush();

    /* 1bpp、宽 16 -> 每行 2 字节，DIB 行按 4 字节对齐，故 stride = 4。 */
    for (y = 0; y < SIM_GLYPH_H; y++)
        for (b = 0; b < 2; b++)
            out[y * 2 + b] = px[(SIM_GLYPH_H - 1 - y) * 4 + b];
}

static int sim_font_init(void) {
    BITMAPINFO bmi;

    memset(&bmi, 0, sizeof bmi);
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = SIM_GLYPH_W;
    bmi.bmiHeader.biHeight = SIM_GLYPH_H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 1;
    bmi.bmiHeader.biCompression = BI_RGB;

    g_glyph_dc = CreateCompatibleDC(NULL);
    if (g_glyph_dc == NULL)
        return 0;

    g_glyph_bmp = CreateDIBSection(g_glyph_dc, &bmi, DIB_RGB_COLORS,
                                   &g_glyph_bits, NULL, 0);
    if (g_glyph_bmp == NULL)
        return 0;

    SelectObject(g_glyph_dc, g_glyph_bmp);

    g_font = CreateFontW(SIM_GLYPH_H, SIM_GLYPH_W, 0, 0, FW_NORMAL, FALSE,
                         FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                         FIXED_PITCH | FF_MODERN, L"Courier New");
    if (g_font == NULL)
        return 0;

    SelectObject(g_glyph_dc, g_font);

    SetBkMode(g_glyph_dc, OPAQUE);
    SetBkColor(g_glyph_dc, RGB(0, 0, 0));
    SetTextColor(g_glyph_dc, RGB(255, 255, 255));
    SetTextAlign(g_glyph_dc, TA_TOP | TA_LEFT);

    /* ---- 全角字形环境：16x16 的 1bpp DIB + 中文字体 ---- */
    memset(&bmi, 0, sizeof bmi);
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = SIM_GLYPH_WIDE_W;
    bmi.bmiHeader.biHeight = SIM_GLYPH_H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 1;
    bmi.bmiHeader.biCompression = BI_RGB;

    g_wide_dc = CreateCompatibleDC(NULL);
    if (g_wide_dc == NULL)
        return 0;

    g_wide_bmp = CreateDIBSection(g_wide_dc, &bmi, DIB_RGB_COLORS,
                                  &g_wide_bits, NULL, 0);
    if (g_wide_bmp == NULL)
        return 0;

    SelectObject(g_wide_dc, g_wide_bmp);

    /* 优先用微软雅黑（Win7+ 都有），失败则回退到通用的 CHARSET 默认字体。 */
    g_wide_font = CreateFontW(SIM_GLYPH_H, SIM_GLYPH_WIDE_W, 0, 0, FW_NORMAL,
                              FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              NONANTIALIASED_QUALITY,
                              FIXED_PITCH | FF_MODERN, L"Microsoft YaHei");
    if (g_wide_font == NULL)
        g_wide_font = CreateFontW(SIM_GLYPH_H, SIM_GLYPH_WIDE_W, 0, 0,
                                  FW_NORMAL, FALSE, FALSE, FALSE,
                                  GB2312_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                                  FIXED_PITCH | FF_MODERN, NULL);
    if (g_wide_font == NULL)
        return 0;

    SelectObject(g_wide_dc, g_wide_font);

    SetBkMode(g_wide_dc, OPAQUE);
    SetBkColor(g_wide_dc, RGB(0, 0, 0));
    SetTextColor(g_wide_dc, RGB(255, 255, 255));
    SetTextAlign(g_wide_dc, TA_TOP | TA_LEFT);

    return 1;
}

static void sim_font_exit(void) {
    if (g_font != NULL)
        DeleteObject(g_font);
    if (g_glyph_bmp != NULL)
        DeleteObject(g_glyph_bmp);
    if (g_glyph_dc != NULL)
        DeleteDC(g_glyph_dc);
    if (g_wide_font != NULL)
        DeleteObject(g_wide_font);
    if (g_wide_bmp != NULL)
        DeleteObject(g_wide_bmp);
    if (g_wide_dc != NULL)
        DeleteDC(g_wide_dc);

    g_font = NULL;
    g_glyph_bmp = NULL;
    g_glyph_dc = NULL;
    g_glyph_bits = NULL;
    g_wide_font = NULL;
    g_wide_bmp = NULL;
    g_wide_dc = NULL;
    g_wide_bits = NULL;
}

/**
 * @brief GDI 回退路径的取字模回调。
 *
 * ch 是解码后的 Unicode 码点。ASCII 走 8x16 等宽表；其余码点（中文等）
 * 走 16x16 全角路径，两者高度相同但宽度不同——正好用来验证标签的
 * 变宽排版（中英文混排时每个字的推进量不同）。
 *
 * 缓存未命中时要付 raster_latency_us：真实 MCU 上光栅化一个字模不便宜。
 */
static uint8_t sim_glyph_gdi(const LEANGL_Font_t *font, LEANGL_CodePoint_t ch,
                             LEANGL_Glyph_t *out) {
    (void)font;

    /* 控制字符不产生字形（\n、\r 由标签排版逻辑处理，不会走到这里）。 */
    if (ch < 0x20u)
        return 0;

    /* ---- ASCII：8x16 ---- */
    if (ch < 0x80u) {
        if (!g_glyph_cache[ch].valid) {
            sim_delay_us(g_cfg.raster_latency_us);
            g_delay_raster_us += g_cfg.raster_latency_us;
            g_raster_calls++;
            sim_render_glyph((uint16_t)ch, g_glyph_cache[ch].bits);
            g_glyph_cache[ch].valid = 1;
        }

        out->size.w = SIM_GLYPH_W;
        out->size.h = SIM_GLYPH_H;
        memcpy(out->bits, g_glyph_cache[ch].bits,
               sizeof g_glyph_cache[ch].bits);
        return 1;
    }

    /* ---- 全角 / CJK：16x16 ---- */
    if (ch > 0xFFFFu)
        return 0; /* BMP 之外（emoji）模拟器不渲染 */

    {
        Sim_Wide_Glyph_t *slot = &g_wide_cache[ch % SIM_WIDE_CACHE];

        if (slot->codepoint != ch) {
            sim_delay_us(g_cfg.raster_latency_us);
            g_delay_raster_us += g_cfg.raster_latency_us;
            g_raster_calls++;
            sim_render_wide_glyph(ch, slot->bits);
            slot->codepoint = ch;
        }

        out->size.w = SIM_GLYPH_WIDE_W;
        out->size.h = SIM_GLYPH_H;
        memcpy(out->bits, slot->bits, sizeof slot->bits);
    }

    return 1;
}

/* 未找到 .hex 字库时的回退字体：8x16 等宽点阵，字模在 sim_glyph_gdi 内部
   按需生成，故 user 为 0。宽度由回调回填的 out->size.w 决定。 */
static const LEANGL_Font_t g_gdi_font =
    LEANGL_FONT_INIT(SIM_GLYPH_H, sim_glyph_gdi, 0);

/* 实际注册给 LeanGL 的字体：所有取模请求先付一次“外部 Flash 读”的延迟，
   再交给底层字库。库自带的字形缓存会把这个开销揾在前面，
   只有缓存未命中时才真的慢。 */
static LEANGL_Font_Glyph_cb g_base_glyph = sim_glyph_gdi;

static uint8_t sim_glyph_slow(const LEANGL_Font_t *font, LEANGL_CodePoint_t ch,
                              LEANGL_Glyph_t *out) {
    sim_delay_us(g_cfg.flash_latency_us);
    g_delay_flash_us += g_cfg.flash_latency_us;
    g_glyph_calls++;

    return g_base_glyph(font, ch, out);
}

static const LEANGL_Font_t g_slow_font =
    LEANGL_FONT_INIT(SIM_GLYPH_H, sim_glyph_slow, 0);

/* ================================================================== */
/* 字库选择：优先 unifont_all-18.0.01.hex，找不到时回退到 GDI                        */
/* ================================================================== */

/**
 * @brief 按顺序尝试若干个默认路径，成功加载则返回所用路径。
 *
 * 路径覆盖常见的几种工作目录：仓库根、tests/、tests/sim/、构建树。
 */
static const char *sim_font_load_default(void) {
    static const char *candidates[] = {
        "tests/assets/unifont_all-18.0.01.hex",      /* cwd = 仓库根目录 */
        "assets/unifont_all-18.0.01.hex",            /* cwd = tests/ */
        "../assets/unifont_all-18.0.01.hex",         /* cwd = tests/sim/ */
        "../../tests/assets/unifont_all-18.0.01.hex",/* cwd = build/host/bin */
        "unifont_all-18.0.01.hex",                   /* cwd 就是字库目录 */
    };
    unsigned int i;

    for (i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
        if (LEANGL_Unifont_Load(candidates[i]))
            return candidates[i];
    }

    return NULL;
}

/* ================================================================== */
/* 屏幕 DIB 与缓冲区                                                    */
/* ================================================================== */

static int sim_screen_init(void) {
    BITMAPINFO bmi;

    memset(&bmi, 0, sizeof bmi);
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)g_cfg.width;
    bmi.bmiHeader.biHeight = -(LONG)g_cfg.height; /* 负数：自上而下，与屏幕坐标一致 */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    g_dib_dc = CreateCompatibleDC(NULL);
    if (g_dib_dc == NULL)
        return 0;

    g_dib = CreateDIBSection(g_dib_dc, &bmi, DIB_RGB_COLORS,
                             (void **)&g_pixels, NULL, 0);
    if (g_dib == NULL)
        return 0;

    SelectObject(g_dib_dc, g_dib);
    memset(g_pixels, 0, (size_t)g_cfg.width * g_cfg.height * 4u);
    return 1;
}

static void sim_screen_exit(void) {
    if (g_dib != NULL)
        DeleteObject(g_dib);
    if (g_dib_dc != NULL)
        DeleteDC(g_dib_dc);

    g_dib = NULL;
    g_dib_dc = NULL;
    g_pixels = NULL;
}

/**
 * @brief 分配 LeanGL 绘图缓冲区、像素带记录表，以及（异步模式的）DMA 副本池。
 */
static int sim_buffer_alloc(void) {
    uint32_t row_bytes = (uint32_t)g_cfg.width * LEANGL_COLOR_BYTES;

    g_framebuf_bytes = row_bytes * g_band_lines * 2u; /* 双缓冲 */
    g_framebuf = (uint8_t *)malloc(g_framebuf_bytes);
    if (g_framebuf == NULL)
        return 0;
    memset(g_framebuf, 0, g_framebuf_bytes);

    g_band_count = (uint16_t)((g_cfg.height + g_band_lines - 1u) /
                              g_band_lines);
    g_band_seen = (uint8_t *)calloc(g_band_count, 1);
    if (g_band_seen == NULL)
        return 0;

#if LEANGL_FLUSH_ASYNC
    g_dma_stride = row_bytes * g_band_lines;
    g_dma_pool = (uint8_t *)malloc(g_dma_stride * SIM_DMA_SLOTS);
    if (g_dma_pool == NULL)
        return 0;
    memset(g_dma_pool, 0, (size_t)g_dma_stride * SIM_DMA_SLOTS);
#endif

    return 1;
}

static void sim_buffer_free(void) {
    free(g_framebuf);
    free(g_band_seen);
    g_framebuf = NULL;
    g_band_seen = NULL;
    g_framebuf_bytes = 0;

#if LEANGL_FLUSH_ASYNC
    free(g_dma_pool);
    g_dma_pool = NULL;
    g_dma_stride = 0;
#endif
}

/* ================================================================== */
/* 像素呈现                                                            */
/* ================================================================== */

/**
 * @brief 把一条像素带（RGB565，小端）转换后写进窗口显存。
 *
 * 同步模式由 flush_cb 直接调用；异步模式在模拟的传输完成时调用。
 */
static void sim_present(uint16_t y, uint16_t h, const uint8_t *data) {
    uint32_t *dst;
    uint32_t i;
    uint32_t n;

    if (g_pixels == NULL || data == NULL || h == 0)
        return;
    if (y >= g_cfg.height)
        return;
    if ((uint32_t)y + h > g_cfg.height)
        h = (uint16_t)(g_cfg.height - y);

    n = (uint32_t)h * g_cfg.width;
    dst = g_pixels + (uint32_t)y * g_cfg.width;

    for (i = 0; i < n; i++) {
        uint16_t c;

        /* 与库的写入端保持一致：按 LEANGL_RGB565_BIG_ENDIAN 组装 16 位值，
           否则配置成高字节在前后，这里用低字节在前解码会得到错误的颜色。 */
#if LEANGL_RGB565_BIG_ENDIAN
        c = (uint16_t)(((uint16_t)data[i * 2] << 8) | data[i * 2 + 1]);
#else
        c = (uint16_t)(data[i * 2] | ((uint16_t)data[i * 2 + 1] << 8));
#endif
        {
            uint32_t r = (c >> 11) & 0x1Fu;
            uint32_t g = (c >> 5) & 0x3Fu;
            uint32_t b = c & 0x1Fu;

            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);

            /* 32bpp BI_RGB 在内存里是 B,G,R,X -> 小端 uint32 为 0xAARRGGBB */
            dst[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }

    /* 记录哪些像素带被真正送到屏幕，自检用它判断有没有饥饿。 */
    if (g_band_seen != NULL && g_band_lines > 0) {
        uint32_t first = (uint32_t)y / g_band_lines;
        uint32_t last = ((uint32_t)y + h - 1u) / g_band_lines;
        uint32_t b;

        for (b = first; b <= last && b < g_band_count; b++)
            g_band_seen[b] = 1;
    }

    g_need_paint = 1;
    g_band_flushes++;
}

/* ================================================================== */
/* 端口层回调                                                          */
/* ================================================================== */

static void sim_flush_cb(uint16_t y, uint16_t h, const uint8_t *data) {
    if (data == NULL || g_band_lines == 0)
        return;

    if (y >= g_cfg.height)
        return;
    if ((uint32_t)y + h > g_cfg.height)
        h = (uint16_t)(g_cfg.height - y);
    if (h == 0)
        return;

#if LEANGL_FLUSH_ASYNC
    /* 异步语义：data 指向库的缓冲区，返回后随时会被改写，所以必须当场拷走。
       真实 DMA 会锁住这块内存，这里用副本池还原同样的效果。 */
    if (g_dma_pool == NULL || g_dma_count >= SIM_DMA_SLOTS)
        return; /* 不该发生：库本身保证最多两块在飞 */

    {
        int slot = (g_dma_head + g_dma_count) % SIM_DMA_SLOTS;
        size_t n = (size_t)h * g_cfg.width * LEANGL_COLOR_BYTES;

        memcpy(g_dma_pool + (size_t)slot * g_dma_stride, data, n);
        g_dma_slot[slot].y = y;
        g_dma_slot[slot].h = h;
        /* 传输在后台进行，完成时间按 flush_latency_us 推算；
           主循环的 sim_dma_poll() 到点后调用 LEANGL_Buf_Ready()。 */
        g_dma_slot[slot].due_us = sim_now_us() + g_cfg.flush_latency_us;
        g_dma_slot[slot].busy = 1;
        g_dma_count++;
    }
#else
    /* 阻塞式：传输真正占用 CPU 时间，返回时像素必须已经在屏上。 */
    sim_delay_us(g_cfg.flush_latency_us);
    g_delay_flush_us += g_cfg.flush_latency_us;
    sim_present(y, h, data);
#endif
}

static uint32_t sim_tick_cb(void) {
    return (uint32_t)(sim_now_us() / 1000u);
}

#if LEANGL_FLUSH_ASYNC
/**
 * @brief 模拟 DMA 传输完成中断：按发起顺序把到期的传输“发完”并通知库。
 *
 * LEANGL_Buf_Ready() 每次清除“最早发起且仍在飞”的那块缓冲区，因此这里的
 * 完成顺序必须与 flush_cb 的调用顺序一致——单通道 DMA 天然如此。
 */
static void sim_dma_poll(void) {
    uint64_t now = sim_now_us();

    while (g_dma_count > 0) {
        Sim_DmaSlot_t *slot = &g_dma_slot[g_dma_head];

        if (!slot->busy)
            break;
        if (now < slot->due_us)
            break; /* 还没传完 */

        g_delay_flush_us += g_cfg.flush_latency_us;
        sim_present(slot->y, slot->h,
                    g_dma_pool + (size_t)g_dma_head * g_dma_stride);
        slot->busy = 0;
        g_dma_head = (g_dma_head + 1) % SIM_DMA_SLOTS;
        g_dma_count--;

        LEANGL_Buf_Ready(); /* 通知库：这块缓冲区可以复用了 */
    }
}

/** @brief 是否还有传输在飞。 */
static int sim_dma_pending(void) {
    return g_dma_count > 0;
}
#else
static void sim_dma_poll(void) {
}

static int sim_dma_pending(void) {
    return 0;
}
#endif

/* ================================================================== */
/* 窗口                                                                */
/* ================================================================== */

static void sim_pointer(int x, int y, int pressed) {
    const LEANGL_Sim_UI_t *ui = leangl_sim_ui();

    if (ui != NULL && ui->pointer != NULL)
        ui->pointer(x, y, pressed);
}

/**
 * @brief 把按键事件转发给界面。
 *
 * 只转发可打印字符（A-Z、0-9），并统一转成小写，方便界面按 'w' / 's' 判断。
 * ESC 由调用方处理（退出），不会走到这里。
 */
static void sim_key(WPARAM vk) {
    const LEANGL_Sim_UI_t *ui = leangl_sim_ui();
    int ch = 0;

    if (vk >= 'A' && vk <= 'Z')
        ch = (int)vk - 'A' + 'a';
    else if (vk >= '0' && vk <= '9')
        ch = (int)vk;

    if (ch != 0 && ui != NULL && ui->key != NULL)
        ui->key(ch);
}

static LRESULT CALLBACK sim_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (g_dib_dc != NULL)
            BitBlt(dc, 0, 0, (int)g_cfg.width, (int)g_cfg.height,
                   g_dib_dc, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        g_need_paint = 0;
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; /* 画面全部由 WM_PAINT 覆盖，避免闪烁 */

    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        g_mouse_down = 1;
        sim_pointer((int)(short)LOWORD(lp), (int)(short)HIWORD(lp), 1);
        return 0;

    case WM_MOUSEMOVE:
        if (g_mouse_down)
            sim_pointer((int)(short)LOWORD(lp), (int)(short)HIWORD(lp), 1);
        return 0;

    case WM_LBUTTONUP:
        ReleaseCapture();
        g_mouse_down = 0;
        sim_pointer((int)(short)LOWORD(lp), (int)(short)HIWORD(lp), 0);
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        } else {
            /* 忽略系统的按键自动重复：界面按自己的节奏处理。 */
            if ((lp & (1L << 30)) == 0)
                sim_key(wp);
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
}

static HWND sim_window_create(HINSTANCE inst) {
    WNDCLASSEXW wc;
    RECT rc;
    HWND hwnd;
    wchar_t title[128];
    char title_a[128];
    DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

    snprintf(title_a, sizeof title_a, "LeanGL %ux%u Simulator [%s]",
             (unsigned)g_cfg.width, (unsigned)g_cfg.height, LEANGL_Sim_Mode());
    MultiByteToWideChar(CP_UTF8, 0, title_a, -1, title, 128);

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = sim_wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"LeanGLSimWindow";

    if (RegisterClassExW(&wc) == 0)
        return NULL;

    /* 让客户区刚好等于屏幕像素尺寸 */
    rc.left = 0;
    rc.top = 0;
    rc.right = (LONG)g_cfg.width;
    rc.bottom = (LONG)g_cfg.height;
    AdjustWindowRect(&rc, style, FALSE);

    hwnd = CreateWindowExW(0, wc.lpszClassName, title, style,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           rc.right - rc.left, rc.bottom - rc.top,
                           NULL, NULL, inst, NULL);
    if (hwnd == NULL)
        return NULL;

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    return hwnd;
}

/* ================================================================== */
/* 画面导出                                                            */
/* ================================================================== */

static void put_u32(FILE *fp, uint32_t v) {
    fputc((int)(v & 0xFF), fp);
    fputc((int)((v >> 8) & 0xFF), fp);
    fputc((int)((v >> 16) & 0xFF), fp);
    fputc((int)((v >> 24) & 0xFF), fp);
}

static int sim_save_bmp(const char *path) {
    FILE *fp;
    uint32_t row_bytes;
    uint32_t data_bytes;
    uint32_t file_bytes;
    uint32_t x;
    uint32_t y;

    if (g_pixels == NULL)
        return 0;

    fp = fopen(path, "wb");
    if (fp == NULL)
        return 0;

    row_bytes = ((uint32_t)g_cfg.width * 3u + 3u) & ~3u;
    data_bytes = row_bytes * g_cfg.height;
    file_bytes = 14u + 40u + data_bytes;

    /* BITMAPFILEHEADER */
    fputc('B', fp);
    fputc('M', fp);
    put_u32(fp, file_bytes);
    put_u32(fp, 0);
    put_u32(fp, 14u + 40u);

    /* BITMAPINFOHEADER */
    put_u32(fp, 40u);
    put_u32(fp, g_cfg.width);
    put_u32(fp, g_cfg.height);
    fputc(1, fp);
    fputc(0, fp);
    fputc(24, fp);
    fputc(0, fp);
    put_u32(fp, 0);
    put_u32(fp, data_bytes);
    put_u32(fp, 2835);
    put_u32(fp, 2835);
    put_u32(fp, 0);
    put_u32(fp, 0);

    /* 像素：BMP 自下而上、BGR 顺序 */
    for (y = g_cfg.height; y-- > 0;) {
        const uint32_t *src = g_pixels + y * g_cfg.width;
        for (x = 0; x < g_cfg.width; x++) {
            uint32_t p = src[x];
            fputc((int)(p & 0xFF), fp);         /* B */
            fputc((int)((p >> 8) & 0xFF), fp);  /* G */
            fputc((int)((p >> 16) & 0xFF), fp); /* R */
        }
        for (x = (uint32_t)g_cfg.width * 3u; x < row_bytes; x++)
            fputc(0, fp);
    }

    fclose(fp);
    return 1;
}

/* ================================================================== */
/* 控制台辅助                                                          */
/* ================================================================== */

static int g_utf8_ok = 0;

/**
 * @brief 让控制台按 UTF-8 解释输出字节。
 *
 * 字符串字面量是 UTF-8，而 Windows 控制台默认代码页通常是 936(GBK)，不切换
 * 的话中文会被按双字节错误解析（“屏幕” → “灞忓箷”）。
 */
static int sim_console_utf8(void) {
    if (SetConsoleOutputCP(CP_UTF8) == 0)
        return 0;
    (void)SetConsoleCP(CP_UTF8);
    return 1;
}

/** @brief 按控制台能力选择中文或英文文案，避免输出乱码。 */
static const char *sim_t(const char *utf8, const char *ascii) {
    return g_utf8_ok ? utf8 : ascii;
}

/* ================================================================== */
/* 命令行                                                              */
/* ================================================================== */

/** @brief 解析 "宽x高"，合法范围 16..4096。 */
static int sim_parse_size(const char *s, uint16_t *w, uint16_t *h) {
    unsigned int a = 0;
    unsigned int b = 0;
    char sep = 0;

    if (sscanf(s, "%u%c%u", &a, &sep, &b) != 3)
        return 0;
    if (sep != 'x' && sep != 'X')
        return 0;
    if (a < 16 || a > 4096 || b < 16 || b > 4096)
        return 0;

    *w = (uint16_t)a;
    *h = (uint16_t)b;
    return 1;
}

/* ================================================================== */
/* 主程序                                                              */
/* ================================================================== */

/* ================================================================== */
/* 命令行帮助                                                          */
/* ================================================================== */

static void sim_usage(void) {
    printf("%s", sim_t(
        "用法：leangl_sim [选项]\n"
        "  --config PATH       配置文件（默认依次查找 sim_config.ini、\n"
        "                      sim/sim_config.ini、tests/sim/sim_config.ini）\n"
        "  --size WxH          屏幕分辨率，例如 240x320\n"
        "  --band-lines N      每条像素带行数，0 表示按高度自动取 1/8\n"
        "  --flush-latency US  每块像素带传屏耗时（微秒）\n"
        "  --no-delay          把所有模拟延迟清零，用主机速度跑\n"
        "  --font PATH         字库 .hex 路径\n"
        "  --verbose           打印配置与逐块刷新日志\n"
        "  --selftest [毫秒]   不开窗口跑到画面稳定（默认 5000ms），全部像素带刷到则返回 0\n"
        "  --dump out.bmp      把当前画面存成 BMP\n",
        "Usage: leangl_sim [options]\n"
        "  --config PATH       config file (default: sim_config.ini,\n"
        "                      sim/sim_config.ini, tests/sim/sim_config.ini)\n"
        "  --size WxH          screen size, e.g. 240x320\n"
        "  --band-lines N      lines per band, 0 = auto (height / 8)\n"
        "  --flush-latency US  per-band transfer time in microseconds\n"
        "  --no-delay          clear all simulated delays\n"
        "  --font PATH         .hex font file\n"
        "  --verbose           print config and per-band log\n"
        "  --selftest [ms]     headless run until settled (default 5000ms),\n"
        "                      exit 0 if every band was flushed\n"
        "  --dump out.bmp      save the frame as BMP\n"));
}

/* ================================================================== */
/* 主程序                                                              */
/* ================================================================== */

int main(int argc, char **argv) {
    HINSTANCE inst = GetModuleHandle(NULL);
    MSG msg;
    int selftest = 0;
    int frames = 5000; /* 自检的时间预算（毫秒） */
    const char *dump_path = NULL;
    const char *font_path = NULL;
    const char *config_path = NULL;
    int running = 1;
    int i;
    const LEANGL_Sim_UI_t *ui;

    g_utf8_ok = sim_console_utf8();
    sim_clock_init();

    /* 第一遍只找 --config：其余参数都要覆盖配置文件，必须先把配置读进来。 */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                printf("%s", sim_t("--config 缺少参数\n", "--config needs a path\n"));
                return 2;
            }
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            sim_usage();
            return 0;
        }
    }

    if (config_path == NULL)
        config_path = Sim_Config_Find_Default();

    if (config_path != NULL) {
        if (!Sim_Config_Load(&g_cfg, config_path))
            printf(sim_t("无法读取配置文件：%s，改用默认值\n",
                         "Cannot read config file: %s, using defaults\n"),
                   config_path);
        else
            printf(sim_t("配置：%s\n", "Config: %s\n"), config_path);
    } else {
        Sim_Config_Init(&g_cfg);
        printf("%s", sim_t("未找到配置文件，使用默认值（主机速度）\n",
                           "No config file found; using defaults (host speed)\n"));
    }

    /* 第二遍：命令行覆盖配置文件。 */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            i++; /* 值已在第一遍读过 */
        } else if (strcmp(argv[i], "--size") == 0) {
            if (i + 1 >= argc ||
                !sim_parse_size(argv[i + 1], &g_cfg.width, &g_cfg.height)) {
                printf("%s", sim_t("--size 需要形如 240x320 的参数\n",
                                   "--size expects WxH, e.g. 240x320\n"));
                return 2;
            }
            i++;
        } else if (strcmp(argv[i], "--band-lines") == 0) {
            int v;

            if (i + 1 >= argc) {
                printf("%s", sim_t("--band-lines 缺少参数\n",
                                   "--band-lines needs a value\n"));
                return 2;
            }
            v = atoi(argv[++i]);
            if (v < 0 || v > 4096) {
                printf("%s", sim_t("--band-lines 超出范围（0..4096）\n",
                                   "--band-lines out of range (0..4096)\n"));
                return 2;
            }
            g_cfg.band_lines = (uint16_t)v;
        } else if (strcmp(argv[i], "--flush-latency") == 0) {
            if (i + 1 >= argc) {
                printf("%s", sim_t("--flush-latency 缺少参数\n",
                                   "--flush-latency needs a value\n"));
                return 2;
            }
            g_cfg.flush_latency_us = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--no-delay") == 0) {
            g_cfg.flash_latency_us = 0;
            g_cfg.raster_latency_us = 0;
            g_cfg.flush_latency_us = 0;
            g_cfg.busy_wait = 0;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            g_cfg.verbose = 1;
        } else if (strcmp(argv[i], "--selftest") == 0) {
            selftest = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                frames = atoi(argv[++i]);
            if (frames <= 0)
                frames = 5000;
        } else if (strcmp(argv[i], "--dump") == 0) {
            if (i + 1 < argc)
                dump_path = argv[++i];
        } else if (strcmp(argv[i], "--font") == 0) {
            if (i + 1 >= argc) {
                printf("%s", sim_t("--font 缺少参数\n", "--font needs a path\n"));
                return 2;
            }
            font_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            /* 第一遍已经处理并退出 */
        } else {
            printf(sim_t("未知参数：%s（--help 查看用法）\n",
                         "Unknown option: %s (see --help)\n"),
                   argv[i]);
            return 2;
        }
    }

    /* 像素带行数：0 表示按高度自动，让整屏大约切成 8 条带。 */
    if (g_cfg.band_lines == 0) {
        uint16_t lines = (uint16_t)(g_cfg.height / 8u);

        if (lines < 16)
            lines = 16;
        if (lines > g_cfg.height)
            lines = g_cfg.height;
        g_band_lines = lines;
    } else {
        g_band_lines = g_cfg.band_lines;
        if (g_band_lines > g_cfg.height)
            g_band_lines = g_cfg.height;
    }
    g_cfg.band_lines = g_band_lines;

    Sim_Config_Print(&g_cfg, g_utf8_ok);

    if (!sim_screen_init()) {
        printf("%s", sim_t("创建屏幕 DIB 失败\n", "Failed to create screen DIB\n"));
        return 1;
    }

    /* --- 字库：优先 .hex，失败时回退到 GDI 现场渲染 --- */
    {
        const char *loaded = NULL;

        /* 命令行 --font 优先于配置文件里的 font.path。 */
        if (font_path == NULL && g_cfg.font_path_set)
            font_path = g_cfg.font_path;

        if (font_path != NULL) {
            if (!LEANGL_Unifont_Load(font_path)) {
                printf(sim_t("加载字库失败：%s\n", "Failed to load font: %s\n"),
                       font_path);
                sim_screen_exit();
                return 1;
            }
            loaded = font_path;
        } else {
            loaded = sim_font_load_default();
        }

        if (loaded != NULL) {
            /* 取模请求走 unifont，读延迟由 sim_glyph_slow 统一加。 */
            g_base_glyph = LEANGL_Unifont_Font()->glyph_cb;
            printf(sim_t("字库：%s（%lu 个字形，unifont）\n",
                         "Font: %s (%lu glyphs, unifont)\n"),
                   loaded, LEANGL_Unifont_Count());
        } else {
            /* 回退：GDI 现场渲染。需要额外的 DIB 与字体对象。 */
            if (!sim_font_init()) {
                printf("%s", sim_t("创建字模渲染环境失败\n",
                                   "Failed to create glyph renderer\n"));
                sim_screen_exit();
                return 1;
            }
            g_base_glyph = g_gdi_font.glyph_cb;
            printf("%s", sim_t(
                "字库：未找到 unifont_all-18.0.01.hex，改用 GDI 渲染（--font 可指定路径）\n",
                "Font: unifont_all-18.0.01.hex not found, using GDI renderer "
                "(--font to specify)\n"));
        }
    }

    if (!sim_buffer_alloc()) {
        printf("%s", sim_t("分配绘图缓冲区失败\n", "Failed to allocate draw buffer\n"));
        LEANGL_Unifont_Unload();
        sim_font_exit();
        sim_screen_exit();
        return 1;
    }

    /* --- LeanGL 初始化（顺序与 leangl.h 的说明一致） --- */
    LEANGL_Set_Screen(g_cfg.width, g_cfg.height);
    LEANGL_Set_Flush_CB(sim_flush_cb);

    /* 统一注册带延迟包装的字体：底层可能是 unifont 或 GDI 回退。 */
    LEANGL_Set_Font(&g_slow_font);

    LEANGL_Set_Tick_CB(sim_tick_cb);
    LEANGL_Buffer_Set(g_framebuf, g_framebuf_bytes, 1);

    printf(sim_t("屏幕 %ux%u，%u 行/块，双缓冲，共 %u 条像素带，刷新模式 %s\n",
                 "Screen %ux%u, %u lines/band, double buffered, %u bands, mode %s\n"),
           (unsigned)g_cfg.width, (unsigned)g_cfg.height,
           (unsigned)g_cfg.band_lines, (unsigned)g_band_count,
           LEANGL_Sim_Mode());

    /* --- 界面 --- */
    ui = leangl_sim_ui();
    if (ui != NULL && ui->build != NULL) {
        printf(sim_t("界面：%s\n", "UI: %s\n"), ui->name != NULL ? ui->name : "-");
        ui->build(g_cfg.width, g_cfg.height);
    } else {
        printf("%s", sim_t("未注册界面，屏幕将保持空白\n", "No UI registered; screen stays blank\n"));
    }

    if (selftest) {
        uint16_t missing = 0;
        uint64_t t_start_us = sim_now_us();
        uint64_t deadline_us = t_start_us + (uint64_t)frames * 1000ull;
        int iters = 0;
        int n;

        /*
         * 先把整屏标脏。自检要验证的是“每条像素带都能刷到”，不能指望界面
         * 恰好把整屏都标脏——很多界面只标自己占用的区域，那样底部若干条带
         * 会一直保持干净，永远轮不到刷新。
         */
        LEANGL_Sim_Refresh();

        /*
         * 按“墙钟时限”而不是“帧数”驱动：开启了延迟模型后，一次主循环可能只
         * 要几十微秒，固定的帧数会在 DMA 传输完成前就耗尽。
         *
         * 退出条件与末尾的断言一致：每条像素带都至少被送到屏幕一次。
         *
         * 不能把“没有在飞的传输”也加进条件：界面上只要有动画（滚动标签等），
         * 两块缓冲就会一直在轮流发送，检查点几乎永远能看到 1 块在飞。
         * 还没传完的块不计入 g_band_seen，多出来的时间由后面的收尾循环兜底。
         */
        while (running && sim_now_us() < deadline_us) {
            sim_dma_poll();

            if (sim_all_bands_seen())
                break;

            LEANGL_Handler();

            if (ui != NULL && ui->update != NULL)
                ui->update(sim_tick_cb());

            iters++;

            if (!g_cfg.busy_wait)
                Sleep(1);
        }

        /* 收尾：把还在飞的传输全部完成（再给 5 秒）。 */
        if (g_cfg.verbose) {
            char bm[64];
            int k;

            for (k = 0; k < g_band_count && k < 63; k++)
                bm[k] = g_band_seen[k] ? '1' : '0';
            bm[k] = '\0';
            printf("loop exit: iters=%d pending=%d bands=%s\n", iters,
                   sim_dma_pending(), bm);
        }

        while (sim_dma_pending() && sim_now_us() < deadline_us + 5000000ull)
            sim_dma_poll();

        for (n = 0; n < g_band_count; n++) {
            if (!g_band_seen[n])
                missing++;
        }

        printf(sim_t("自检完成：%d 轮，%u 条像素带被刷新，%u 条缺失，脏区%s\n",
                     "Self-test: %d rounds, %u bands flushed, %u missing, dirty %s\n"),
               iters, (unsigned)g_band_flushes, (unsigned)missing,
               LEANGL_Dirty_Is_Empty()
                   ? sim_t("已清空", "cleared")
                   : sim_t("仍非空", "pending"));

        printf(sim_t("模拟耗时：字库 %llu us（%u 次取模），光栅 %llu us（%u 次），"
                     "刷屏 %llu us，实际 %llu ms\n",
                     "Simulated: flash %llu us (%u calls), raster %llu us (%u calls), "
                     "flush %llu us, wall %llu ms\n"),
               (unsigned long long)g_delay_flash_us, (unsigned)g_glyph_calls,
               (unsigned long long)g_delay_raster_us, (unsigned)g_raster_calls,
               (unsigned long long)g_delay_flush_us,
               (unsigned long long)((sim_now_us() - t_start_us) / 1000u));

        if (dump_path != NULL)
            printf(sim_t("画面已写入 %s：%s\n", "Frame written to %s: %s\n"),
                   dump_path,
                   sim_save_bmp(dump_path)
                       ? sim_t("成功", "ok")
                       : sim_t("失败", "failed"));

        sim_buffer_free();
        LEANGL_Unifont_Unload();
        sim_font_exit();
        sim_screen_exit();
        return missing == 0 ? 0 : 1;
    }

    /* --- 交互模式 --- */
    g_hwnd = sim_window_create(inst);
    if (g_hwnd == NULL) {
        printf("%s", sim_t("创建窗口失败\n", "Failed to create window\n"));
        sim_buffer_free();
        LEANGL_Unifont_Unload();
        sim_font_exit();
        sim_screen_exit();
        return 1;
    }

    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = 0;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running || g_quit)
            break;

        sim_dma_poll();   /* 模拟 DMA 完成中断 */
        LEANGL_Handler(); /* 事件处理 + 分块刷新 */

        if (ui != NULL && ui->update != NULL)
            ui->update(sim_tick_cb());

        if (g_need_paint) {
            g_need_paint = 0;
            InvalidateRect(g_hwnd, NULL, FALSE);
        }

        /* 交互模式固定让出一个时间片：需要验证的延迟已经在取模与刷屏里了，
           空闲自旋对观感没有帮助。selftest 则严格按 busy_wait 设定跑。 */
        Sleep(1);
    }

    sim_buffer_free();
    LEANGL_Unifont_Unload();
    sim_font_exit();
    sim_screen_exit();
    return 0;
}
