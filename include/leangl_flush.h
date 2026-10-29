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

#ifndef LEANGL_FLUSH_H
#define LEANGL_FLUSH_H

#include <stdint.h>

/**
 * @file    leangl_flush.h
 * @brief   刷新调度：按脏区分块、调用控件绘制并发送到屏幕。
 *
 * 控件由 LEANGL_Widget_Process() 周期处理，内容变化的控件把区域加入脏区；
 * LEANGL_Flush() 再把脏区按缓冲区行数分块，设置当前刷新窗口，调用与当前块
 * 相交的控件绘制函数，最后经 LEANGL_Flush_Buf() 发送该块。
 */

/**
 * @brief 当前刷新块覆盖的屏幕行范围。
 *
 * 由刷新模块在绘制每一块前填写，绘制函数只读；调用端不需要直接使用。
 */
typedef struct {
    uint16_t y0;   /**< 块起始行（屏幕坐标） */
    uint16_t rows; /**< 块覆盖的行数 */
} LEANGL_Flush_t;

/**
 * @brief 周期处理入口，建议在主循环中反复调用。
 *
 * 每次调用做两件事：
 *   1. 若毫秒时钟已过 LEANGL_EVENT_PERIOD_MS，调用 LEANGL_Widget_Process()：
 *      逐个执行控件的 event_cb，返回非 0 的控件把自己的区域加入脏区；
 *   2. 调用 LEANGL_Flush()：按脏区分块，调用控件的 draw_cb 绘制并发送。
 *
 * 因此主循环里只调本函数即可，不需要再手动调 LEANGL_Widget_Process() 或
 * LEANGL_Flush()。本函数不能在中断中调用；flush_cb 为阻塞实现时，耗时与
 * 本次需要重绘的行数成正比。
 *
 * 未设置毫秒时钟回调时，必须使用一个定时器来定期调用LEANGL_Tick_Inc(uint32_t
 * ms)。
 */
void LEANGL_Handler(void);

/**
 * @brief 增加now的值，如果没有设置；毫秒时钟回调，必须在定时器里
 * 调用这个函数，否则LEANGL_Handler将不会工作 */
void LEANGL_Tick_Inc(uint32_t ms);

/**
 * @brief 处理脏区并刷新屏幕。
 *
 * 把屏幕按缓冲区行数分块，只处理与脏区相交的块：调用与该块相交的控件绘制
 * 函数，然后发送该块。没有脏区、未设置 flush_cb 或缓冲区未初始化时什么也
 * 不做。
 *
 * 已提交的块会立即从脏区表里按行清除（不是整轮结束后一次性清空），因此：
 *   - 扫描期间新产生的脏标记（包括落在已刷行上的）不会被误清；
 *   - 中途退出时，已经发出去的块不会在下一次被重画。
 *
 * 异步模式（LEANGL_FLUSH_ASYNC = 1）下，如果轮到的那块缓冲区仍在发送，本轮
 * 会停在这里，但会记住停在哪一块；下一次 LEANGL_Handler() 从该块继续，
 * 而不是从屏幕顶部重来。因此即使一次 DMA 只能容纳一块，整屏最终也能刷完。
 * 阻塞模式下活动缓冲区总是空闲，一轮即可扫完。
 */
void LEANGL_Flush(void);

#endif
