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

#ifndef LEANGL_PORT_H
#define LEANGL_PORT_H

#include <stdint.h>

#include "leangl_font.h"

/**
 * @file    leangl_port.h
 * @brief   LeanGL 端口层（硬件移植接口）的配置入口。
 *
 * LeanGL 的绘制代码不直接访问硬件：像素输出经由 LEANGL_Port_t 中登记的
 * flush_cb 完成，字模则通过 LEANGL_Set_Font() 注册的字体对象取得。换屏或
 * 换平台时只需重写这一层，core / draw 中的代码不必改动。
 *
 * 典型接入顺序：
 *   1. 调用 LEANGL_Set_Screen() 设置屏幕分辨率；
 *   2. 调用 LEANGL_Set_Flush_CB() / LEANGL_Set_Tick_CB() 登记回调；
 *   3. 调用 LEANGL_Set_Font() 注册字体（见 leangl_font.h）；
 *   4. 调用 LEANGL_Buffer_Set() 分配绘图缓冲区，每块的行数由屏幕宽度换算；
 *   5. 此后每次 LEANGL_Flush() 都会经 flush_cb 把像素送到屏幕。
 *
 * @note 第 1 步必须先于第 3 步完成，否则行数换算会除零。
 */

/**
 * @brief 屏幕分辨率。
 *
 * 必须填写屏幕的真实像素尺寸：缓冲区每行的像素数、分块行数以及绘制时的
 * 越界判断，都以这里的 width / height 为准。
 * 坐标原点在左上角，x 向右递增，y 向下递增。
 */
typedef struct{
    uint32_t width;   /**< 屏幕宽度（像素），即 flush_cb 每次输出的像素带宽度。 */
    uint32_t height;  /**< 屏幕高度（像素）。 */
} LEANGL_Screen_t;

/**
 * @brief 端口层回调集合。
 *
 * 由库内部持有。调用端通过 LEANGL_Set_Screen()、LEANGL_Set_Flush_CB()、
 * LEANGL_Set_Font()、LEANGL_Set_Tick_CB() 完成配置，不需要直接访问
 * 本结构体。
 */
typedef struct {
    LEANGL_Screen_t screen;  /**< 屏幕分辨率，决定刷新宽度与缓冲区分块。 */

    /**
     * @brief 刷新回调，把一条满宽的横向像素带发送到显示屏。
     *
     * 像素带横跨整个屏幕：起始列恒为 0，宽度恒等于 screen.width，因此位置
     * 只用纵向坐标表示。像素带从第 y 行开始，向下共 h 行。
     *
     * @c data 指向 h * screen.width 个连续像素，像素格式由 leanGL_conf.h 中的
     * LEANGL_COLOR_FORMAT 决定（RGB565 时为 2 字节/像素）。
     *
     * RGB565 的高低字节顺序由 leanGL_conf.h 中的 LEANGL_RGB565_BIG_ENDIAN
     * 控制：置 1 时高字节在前（多数 LCD 控制器期望的顺序），置 0 时低字节在前
     * （与 uint16_t 在小端平台上的内存布局一致）。如果屏上的颜色“红蓝互换”，
     * 把这个选项调反即可，不需要改代码。
     *
     * 阻塞式实现（默认，LEANGL_FLUSH_ASYNC = 0）：函数返回时像素带必须已经
     * 发送完毕，库会在返回后立即复用该缓冲区。
     *
     * 若用 DMA 异步发送，必须把 leanGL_conf.h 中的 LEANGL_FLUSH_ASYNC 设为 1，
     * 并在传输完成后调用 LEANGL_Buf_Ready()（可在 DMA 完成中断中调用），
     * 告知库该缓冲区已可重用；否则库会认为这块缓冲区一直忙，刷新会停在原地。
     *
     * @param y    像素带的起始行（纵坐标）。
     * @param h    像素带的行数，即缓冲区分块的行数。
     * @param data 像素数据首地址，共 h * screen.width 个像素。
     */
    void (*flush_cb)(uint16_t y, uint16_t h, const uint8_t *data);

    /**
     * @brief 当前字体。
     *
     * 由 LEANGL_Set_Font() 设置，本结构体不直接暴露给应用。
     * 为 NULL 时所有文本绘制都会被跳过（不报错）。
     */
    const LEANGL_Font_t *font;

    /**
     * @brief 毫秒时钟回调。
     *
     * 返回自系统启动以来的毫秒数，LEANGL_Handler() 用它判断事件处理周期。
     * 置 NULL 时退化为每次调用 LEANGL_Handler() 都处理一次事件。
     */
    uint32_t (*tick_cb)(void);
} LEANGL_Port_t;

/**
 * @brief 设置屏幕分辨率。
 *
 * 必须在 LEANGL_Buffer_Set() 之前调用；宽度为 0 会导致缓冲区分块时除零。
 *
 * @param width  屏幕宽度（像素）。
 * @param height 屏幕高度（像素）。
 */
void LEANGL_Set_Screen(uint32_t width, uint32_t height);

/**
 * @brief 设置刷新回调。
 *
 * 用于在运行期替换当前的刷新回调，例如在多个显示后端之间切换。
 * 回调的调用时机、像素格式与缓冲区释放要求见 LEANGL_Port_t::flush_cb。
 *
 * @param flush_cb 新的刷新回调函数。
 */
void LEANGL_Set_Flush_CB(void (*flush_cb)(uint16_t y, uint16_t h, const uint8_t *data));

/**
 * @brief 设置毫秒时钟回调。
 *
 * LEANGL_Handler() 通过它取得当前毫秒数，按 LEANGL_EVENT_PERIOD_MS 的周期
 * 触发控件事件处理。
 *
 * @param tick_cb 返回毫秒计数的回调；NULL 表示不使用周期调度。
 */
void LEANGL_Set_Tick_CB(uint32_t (*tick_cb)(void));

#endif
