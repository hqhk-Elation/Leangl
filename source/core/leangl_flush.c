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

#include "leangl_flush.h"
#include "leangl_buffer.h"
#include "leangl_conf.h"
#include "leangl_dirty.h"
#include "leangl_internal.h"
#include "leangl_port.h"
#include "leangl_widget.h"
#include <stddef.h>
#include <stdint.h>

LEANGL_Flush_t leangl_flush = {0, 0};
static uint32_t s_last_tick = 0;
static uint8_t s_in_flush = 0;
static volatile uint32_t now = 0;

/* 下一轮扫描的起点（屏幕行）。异步模式下缓冲区忙时停在半路，
   这个游标让下一次从断点继续，而不是从第 0 行重来。 */
static uint32_t s_next_row = 0;

void LEANGL_Handler(void) {
    uint32_t tick = now;
    if (leangl_port.tick_cb != NULL) {
        tick = leangl_port.tick_cb();
    }
    if ((uint32_t)(tick - s_last_tick) >= LEANGL_EVENT_PERIOD_MS) {
        s_last_tick = tick;
        LEANGL_Widget_Process();
    }
    LEANGL_Flush();
}

void LEANGL_Tick_Inc(uint32_t ms) {
    if (leangl_port.tick_cb != NULL)
        return;
    now += ms;
}

void LEANGL_Flush(void) {
    uint32_t y;
    uint8_t complete = 1;

    if (s_in_flush)
        return;
    if (LEANGL_Dirty_Is_Empty()) {
        s_next_row = 0; /* 脏区已被外部清空，游标作废 */
        return;
    }
    if (leangl_port.flush_cb == NULL)
        return;
    if (buffer.lines == 0 || leangl_port.screen.height == 0)
        return;

    s_in_flush = 1;

    for (y = s_next_row; y < leangl_port.screen.height; y += buffer.lines) {
        uint16_t y0 = (uint16_t)y;
        uint16_t rows = buffer.lines;

        if (leangl_port.screen.height - y < rows)
            rows = (uint16_t)(leangl_port.screen.height - y);

        if (!LEANGL_Dirty_Intersects(y0, (uint16_t)(y0 + rows - 1)))
            continue;

        /* 活动缓冲区仍在发送（异步/DMA）：不能覆盖它。
           保留游标，下一次 LEANGL_Handler() 从这一块接着处理，
           已经发出去的块不会被重画。 */
        if (!LEANGL_Buf_Is_Ready()) {
            complete = 0;
            break;
        }

        LEANGL_Widget_Draw(y0, (uint16_t)(y0 + rows - 1));
        LEANGL_Flush_Buf(y0, (uint16_t)(y0 + rows - 1));

        /* 逐块清脏：本块已提交，立即从脏区表里抹掉。
           扫描期间新产生的脏标记（甚至落在已刷行上）不会被误清。 */
        LEANGL_Dirty_Clear_Range(y0, (uint16_t)(y0 + rows - 1));
    }

    leangl_flush.y0 = 0;
    leangl_flush.rows = 0;

    /* 整轮扫完才复位游标；中途因缓冲区忙退出时保留进度。 */
    if (complete)
        s_next_row = 0;
    else
        s_next_row = y;

    s_in_flush = 0;
}
