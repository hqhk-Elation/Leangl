#ifndef LEANGL_COLORS_H
#define LEANGL_COLORS_H

#include "leangl_conf.h"
#include <stdint.h>

#if LEANGL_COLOR_FORMAT_RGB888
/**
 * @brief 颜色值类型，随 LEANGL_COLOR_FORMAT 变化。
 */
typedef uint32_t LEANGL_Color_t;
#define LEANGL_Color_Hex(col) ((LEANGL_Color_t)(col))
#define LEANGL_COLOR_BYTES 3
#elif LEANGL_COLOR_FORMAT_RGB565
/**
 * @brief 颜色值类型，随 LEANGL_COLOR_FORMAT 变化。
 */
typedef uint16_t LEANGL_Color_t;
/**
 * @brief 把 0xRRGGBB 转成 RGB565。
 *
 * 三个分量各用按位与取出后右移到目标位置，再或在一起：
 *   - R 取高 5 位，落在 bit15..11；
 *   - G 取高 6 位，落在 bit10..5；
 *   - B 取高 5 位，落在 bit4..0。
 *
 * 每一项都加了外层括号，避免与外部表达式混用时被运算符优先级改写。
 */
#define LEANGL_Color_Hex(col) (                            \
    ((((col) & 0xFF0000u) >> 8) & 0xF800u) |               \
    ((((col) & 0x00FF00u) >> 5) & 0x07E0u) |               \
    (((col) & 0x0000FFu) >> 3))
#define LEANGL_COLOR_BYTES 2
#else
#error "没有定义 LEANGL_COLOR_FORMAT_RGB565 / LEANGL_COLOR_FORMAT_RGB888"
#endif

#define LEANGL_COLOR_BLACK LEANGL_Color_Hex (0x000000)
#define LEANGL_COLOR_WHITE LEANGL_Color_Hex (0xFFFFFF)
#define LEANGL_COLOR_RED LEANGL_Color_Hex (0xFF0000)
#define LEANGL_COLOR_GREEN LEANGL_Color_Hex (0x00FF00)
#define LEANGL_COLOR_BLUE LEANGL_Color_Hex (0x0000FF)
#define LEANGL_COLOR_YELLOW LEANGL_Color_Hex (0xFFFF00)
#define LEANGL_COLOR_CYAN LEANGL_Color_Hex (0x00FFFF)
#define LEANGL_COLOR_MAGENTA LEANGL_Color_Hex (0xFF00FF)

#endif