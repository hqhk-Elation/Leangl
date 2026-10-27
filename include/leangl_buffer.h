#ifndef LEANGL_BUFFER_H
#define LEANGL_BUFFER_H
#include <stdint.h>

/**
 * @file    leangl_buffer.h
 * @brief   绘图缓冲区（帧缓冲）的配置与刷新接口。
 *
 * 存储空间由使用者提供。缓冲区是一块横向条带：宽 screen.width 像素、高
 * buffer.lines 行。它当前对应屏幕的哪几行由刷新模块管理，本模块只负责按
 * 整行切分、标记忙状态并调用端口层的刷新回调。
 *
 * 缓冲区按整行划分：每块大小是 screen.width * LEANGL_COLOR_BYTES 的整数倍，
 * 不足一行的尾部空间会被忽略。内容按打包像素存放，每个像素占
 * LEANGL_COLOR_BYTES 字节。像素字节序由 leangl_conf.h 中的
 * LEANGL_RGB565_BIG_ENDIAN 决定（仅 RGB565 meaningful；RGB888 固定 R、G、B 顺序）。
 */

/**
 * @brief 绘图缓冲区管理结构。
 *
 * 记录缓冲区地址、大小及忙标志，供绘制与刷新流程共享。单缓冲时只使用
 * buffer[0]；双缓冲时两块等大的缓冲区在 LEANGL_Buffer_Set() 传入的连续空间
 * 中按整行切分。
 */
typedef struct {
    uint8_t *buffer[2];     /**< 缓冲区首地址，单缓冲时只用 buffer[0] */
    uint32_t buffer_size;   /**< 单块缓冲区的字节数，为整行字节数的整数倍 */
    volatile uint8_t busy[2]; /**< 各缓冲区的忙标志，非 0 表示已交给硬件、尚未发送完毕 */
    uint16_t lines;         /**< 每块缓冲区可容纳的像素行数 */
    uint8_t use_double_buf; /**< 是否启用双缓冲，1 启用，0 为单缓冲 */
} LEANGL_Buffer_t;

/**
 * @brief 初始化绘图缓冲区。
 *
 * 按 use_double_buf 把 buf 指向的空间登记为一块或两块缓冲区。缓冲区以整行为
 * 单位，每块大小取 screen.width * LEANGL_COLOR_BYTES 的整数倍，可容纳的行数
 * 记入 buffer.lines；不足一行的尾部空间会被忽略。两块缓冲区的忙标志会被清零。
 *
 * @param buf            缓冲区存储区首地址；双缓冲时两块等大的缓冲区连续排列。
 * @param buf_size       buf 所指空间的总字节数；双缓冲时按两块等大整行块切分。
 * @param use_double_buf 是否启用双缓冲，非 0 启用，0 为单缓冲。
 *
 * @note 调用前必须先用 LEANGL_Set_Screen() 设置屏幕尺寸，否则行数换算会除零。
 * @note 本函数不清空缓冲区内容，也不负责分配内存。
 */
void LEANGL_Buffer_Set(void *buf, uint32_t buf_size, uint8_t use_double_buf);

/**
 * @brief 获取当前活动缓冲区的首地址。
 *
 * 返回接下来应当写入的那一块；双缓冲下每调用一次 LEANGL_Flush_Buf()，
 * 活动块即切换到另一块。
 *
 * @return 活动缓冲区首地址。
 *
 * @note 本函数不检查忙标志，返回的缓冲区仍可能处于发送中；写入前可用
 *       LEANGL_Buf_Is_Ready() 确认。
 */
uint8_t *LEANGL_Get_Buf();

/**
 * @brief 通知库：已交给硬件的缓冲区发送完毕。
 *
 * 清除“最早发起、仍在发送”的那块缓冲区的忙标志，使其可以重新用于绘制。
 * 在 flush_cb 的传输完成回调（或 DMA 完成中断）中调用。
 *
 * @note 仅在 leanGL_conf.h 中 LEANGL_FLUSH_ASYNC 为 1 时有效。阻塞式刷新
 *       （默认，LEANGL_FLUSH_ASYNC = 0）由库在 flush_cb 返回后自动清忙，
 *       不需要也不应该调用本函数。
 * @note 完成顺序必须与 LEANGL_Flush_Buf() 的发起顺序一致（单通道 DMA 天然
 *       如此）。若连续发起两次传输却只调用一次本函数，另一块仍会停留在忙状态。
 */
void LEANGL_Buf_Ready();

/**
 * @brief 查询当前活动缓冲区是否空闲。
 *
 * @return 1 表示活动缓冲区未处于发送中，可以继续绘制或发起下一次刷新；
 *         0 表示它仍在等待 LEANGL_Buf_Ready()（只有异步模式才会出现）。
 */
uint8_t LEANGL_Buf_Is_Ready();

/**
 * @brief 把活动缓冲区中的一段像素行发送到显示屏。
 *
 * 发送前把该缓冲区标记为忙，调用端口层注册的刷新回调输出；若启用双缓冲，
 * 发送后把活动缓冲区切换到另一块。
 *
 * @param y1 起始行（屏幕纵坐标）。
 * @param y2 结束行（屏幕纵坐标，含）。
 *
 * @note 发送的行数会被截断到缓冲区可容纳的行数；未设置 flush_cb 时什么也不做。
 * @note 活动缓冲区仍在发送时本次调用会被忽略（正常流程由 LEANGL_Flush() 的
 *       就绪检查拦住，直接调用时才可能遇到）。
 * @note 异步模式（LEANGL_FLUSH_ASYNC = 1）下 flush_cb 传输完成后必须调用
 *       LEANGL_Buf_Ready()，否则该缓冲区不会再被复用；阻塞模式下库会自动清忙。
 */
void LEANGL_Flush_Buf(const uint16_t y1, const uint16_t y2);

#endif
