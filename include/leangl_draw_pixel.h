#ifndef LEANGL_DRAW_PIXEL_H
#define LEANGL_DRAW_PIXEL_H

#include "leangl_colors.h"
#include "leangl_conf.h"
#include <stdint.h>

/**
 * @file    leangl_draw_pixel.h
 * @brief   像素级写入：按当前颜色格式把颜色值写进缓冲区。
 *
 * 本文件只负责“颜色值 → 缓冲区字节”的转换，不计算坐标、不做边界检查，
 * 也不决定写入哪一块缓冲区；这些由 draw_shape、draw_char 等调用方完成。
 *
 * LEANGL_Pixel_Write() 为 static inline，颜色格式与字节序都用 #if 在编译期
 * 选定，因此写入一个像素只有一次地址计算和一到两次字节写，没有函数调用和
 * 运行时分支的开销。
 */

/**
 * @brief 把 RGB565 的 16 位值展开成两个字节，顺序由配置决定。
 *
 * 单独抽出来，是为了让“字节序”这件事只有一处定义：
 *   - LEANGL_Pixel_Write()：逐像素写入时用；
 *   - 需要批量转换的场合（如整带填充、DMA 前处理）也能复用同一套规则。
 *
 * @param color 输出：按内存顺序排好的 2 字节，dst[0] 先发送。
 * @param c     源颜色（RGB565，高 5 位为红）。
 */
static inline void LEANGL_RGB565_To_Bytes(uint8_t *dst, uint16_t c) {
#if LEANGL_RGB565_BIG_ENDIAN
    /* 高字节在前：很多 LCD 控制器的期望顺序。 */
    dst[0] = (uint8_t)(c >> 8);
    dst[1] = (uint8_t)(c & 0xFFu);
#else
    /* 低字节在前：与 uint16_t 在小端平台上的内存布局一致。 */
    dst[0] = (uint8_t)(c & 0xFFu);
    dst[1] = (uint8_t)(c >> 8);
#endif
}

/**
 * @brief 把一个像素写到 dst 指向的缓冲区位置。
 *
 * 写入的字节数由 LEANGL_COLOR_BYTES 决定。RGB888 固定按 R、G、B 顺序存放；
 * RGB565 的字节序由 leangl_conf.h 中的 LEANGL_RGB565_BIG_ENDIAN 决定。
 *
 * dst 必须指向缓冲区内一个合法像素的起始地址，本函数不做越界检查。
 *
 * 行号按当前刷新块折算：屏幕第 y 行对应缓冲区内的第 y - y0 行，y0 与可见
 * 行数由刷新模块在内部维护。
 *
 * @param dst   目标像素地址，由调用方按行、列算出字节偏移。
 * @param color 颜色值，类型随 LEANGL_COLOR_FORMAT 为 uint16_t 或 uint32_t。
 */
static inline void LEANGL_Pixel_Write(uint8_t *dst, LEANGL_Color_t color) {
#if LEANGL_COLOR_FORMAT_RGB888
    dst[0] = (uint8_t)(color >> 16);
    dst[1] = (uint8_t)(color >> 8);
    dst[2] = (uint8_t)(color);
#elif LEANGL_COLOR_FORMAT_RGB565
    LEANGL_RGB565_To_Bytes(dst, (uint16_t)color);
#else
#error "没有定义 LEANGL_COLOR_FORMAT_RGB565 / LEANGL_COLOR_FORMAT_RGB888"
#endif
}

#endif
