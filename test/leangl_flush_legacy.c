/**
 * @file    leangl_flush_legacy.c
 * @brief   【临时对照用】LEANGL_Flush() 修复前的原始实现。
 *
 * 保留这个文件是为了在真机上直观对比修复前后的行为差异。它和
 * source/core/leangl_flush.c 定义同名符号（LEANGL_Flush / LEANGL_Handler /
 * LEANGL_Tick_Inc / leangl_flush），因此**不能与后者同时参与链接**。
 *
 * test/CMakeLists.txt 里的 leangl_win32_sim_legacy 目标专门把它替换掉
 * source/core/leangl_flush.c 来构建，两者互不影响。
 *
 * 已知差异（修复前）：
 *   1. 异步模式下没有扫描游标，缓冲区一忙就 break 并整轮重来，导致后面的
 *      像素带永远轮不到（饥饿）；屏幕上方几条带反复重绘、下方完全冻结。
 *   2. 只有整轮扫完才 LEANGL_Dirty_Clear()，中途退出时已提交的块会在下一轮
 *      被重复绘制和重复发送。
 *
 * 这个文件在对照测试完成后应当删除。
 *
 * 不要把本文件加入产品库的源文件清单。
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
    if (LEANGL_Dirty_Is_Empty())
        return;
    if (leangl_port.flush_cb == NULL)
        return;
    if (buffer.lines == 0 || leangl_port.screen.height == 0)
        return;

    s_in_flush = 1;

    /* 关键差异：每轮都从屏幕顶部 y = 0 重新扫描。 */
    for (y = 0; y < leangl_port.screen.height; y += buffer.lines) {
        uint16_t y0 = (uint16_t)y;
        uint16_t rows = buffer.lines;

        if (leangl_port.screen.height - y < rows)
            rows = (uint16_t)(leangl_port.screen.height - y);

        if (!LEANGL_Dirty_Intersects(y0, (uint16_t)(y0 + rows - 1)))
            continue;

        /* 活动缓冲区仍在发送：直接放弃本轮剩余部分，且不记录进度。 */
        if (!LEANGL_Buf_Is_Ready()) {
            complete = 0;
            break;
        }

        leangl_flush.y0 = y0;
        leangl_flush.rows = rows;
        LEANGL_Widget_Draw(y0, (uint16_t)(y0 + rows - 1));
        LEANGL_Flush_Buf(y0, (uint16_t)(y0 + rows - 1));

        /* 关键差异：这里不逐块清脏，已提交的块没有任何完成记录。 */
    }

    leangl_flush.y0 = 0;
    leangl_flush.rows = 0;

    /* 只有整轮都刷完才清脏区；中途因缓冲区忙退出时，下次还要重画这些区域。 */
    if (complete)
        LEANGL_Dirty_Clear();

    s_in_flush = 0;
}
