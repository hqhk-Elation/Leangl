#include "leangl_buffer.h"
#include "leangl_colors.h"
#include "leangl_conf.h"
#include "leangl_internal.h"
#include "leangl_port.h"
#include <stddef.h>
#include <stdint.h>

LEANGL_Buffer_t buffer = {0};
static uint8_t s_active = 0;

#if LEANGL_FLUSH_ASYNC
/* 已交给硬件、尚未完成的传输。s_flight[i] 非 0 时保存该缓冲区本次传输的序号，
   序号最小的就是最早发起、也应当最早完成的一块。
   序号只在主循环里写、在完成中断里读；主循环不会碰仍在发送中的缓冲区，
   所以这里不需要关中断。 */
static volatile uint32_t s_flight[2] = {0, 0};
static uint32_t s_next_seq = 1;
#endif

static void buffer_reset(void) {
    buffer.buffer[0] = NULL;
    buffer.buffer[1] = NULL;
    buffer.buffer_size = 0;
    buffer.lines = 0;
    buffer.busy[0] = 0;
    buffer.busy[1] = 0;
    buffer.use_double_buf = 0;
    s_active = 0;
#if LEANGL_FLUSH_ASYNC
    s_flight[0] = 0;
    s_flight[1] = 0;
    s_next_seq = 1;
#endif
}

void LEANGL_Buffer_Set(void *buf, uint32_t buf_size, uint8_t use_double_buf) {
    uint32_t row_bytes;
    uint32_t chunk;
    uint32_t lines;

    buffer_reset();

    /* 参数不合法时保持“未初始化”状态，LEANGL_Flush() 会直接返回，
       不会再去碰空指针或者除零。 */
    if (buf == NULL || buf_size == 0)
        return;

    row_bytes = leangl_port.screen.width * LEANGL_COLOR_BYTES;
    if (row_bytes == 0) /* 必须先调 LEANGL_Set_Screen() */
        return;

    chunk = use_double_buf ? (buf_size / 2u) : buf_size;
    lines = chunk / row_bytes;

    if (lines == 0) /* 连一整行都放不下 */
        return;
    if (lines > UINT16_MAX)
        lines = UINT16_MAX;

    buffer.use_double_buf = use_double_buf ? 1 : 0;
    buffer.lines = (uint16_t)lines;
    buffer.buffer_size = lines * row_bytes;
    buffer.buffer[0] = (uint8_t *)buf;
    buffer.buffer[1] = buffer.use_double_buf
                           ? (uint8_t *)buf + buffer.buffer_size
                           : (uint8_t *)0;
}

uint8_t *LEANGL_Get_Buf() {
    return buffer.buffer[s_active];
}

void LEANGL_Buf_Ready() {
#if LEANGL_FLUSH_ASYNC
    uint8_t i;
    uint8_t oldest = 0xFF;
    uint32_t oldest_seq = 0;

    /* 完成顺序与发起顺序一致，所以“最早发起且仍在发送”的那块就是本次完成的。 */
    for (i = 0; i < 2; i++) {
        if (s_flight[i] != 0 &&
            (oldest == 0xFF || s_flight[i] < oldest_seq)) {
            oldest = i;
            oldest_seq = s_flight[i];
        }
    }

    if (oldest == 0xFF)
        return;

    s_flight[oldest] = 0;
    buffer.busy[oldest] = 0;
#endif
}

uint8_t LEANGL_Buf_Is_Ready() {
    return (uint8_t)!buffer.busy[s_active];
}

void LEANGL_Flush_Buf(const uint16_t y1, const uint16_t y2) {
    uint32_t h;
    uint8_t idx;

    if (y2 < y1)
        return;
    if (leangl_port.flush_cb == NULL)
        return;

    h = (uint32_t)y2 - y1 + 1;
    if (h > buffer.lines)
        h = buffer.lines;
    if (h == 0)
        return;

    idx = s_active;
    if (buffer.busy[idx])
        return; /* 该缓冲区仍在发送；正常流程由 LEANGL_Flush() 拦住 */

    buffer.busy[idx] = 1;

#if LEANGL_FLUSH_ASYNC
    /* 先登记再启动传输，防止传输很快完成、中断在登记之前到达。 */
    s_flight[idx] = s_next_seq;
    s_next_seq++;
    if (s_next_seq == 0) /* 0 表示空闲，跳过这个值 */
        s_next_seq = 1;
#endif

    leangl_port.flush_cb(y1, (uint16_t)h, buffer.buffer[idx]);

#if !LEANGL_FLUSH_ASYNC
    buffer.busy[idx] = 0; /* 阻塞式回调：返回即可复用 */
#endif

    if (buffer.use_double_buf)
        s_active = (uint8_t)(s_active ^ 1u);
}
