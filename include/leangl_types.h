#ifndef LEANGL_TYPES_H
#define LEANGL_TYPES_H

#include <stdint.h>

/**
 * @brief 矩形区域描述。
 *
 * 使用左上角与右下角两个点表示一块矩形区域。
 */
typedef struct
{
    uint16_t x1, y1;  /**< 左上角坐标 */
    uint16_t x2, y2;  /**< 右下角坐标 */
}LEANGL_Rect_t;

/**
 * @brief 布尔类型定义。
 */
typedef enum{
    LEANGL_FALSE = 0,  /**< 假 */
    LEANGL_TRUE = 1    /**< 真 */
}LEANGL_Bool_t;

#endif