#ifndef LEANGL_DIRTY_H
#define LEANGL_DIRTY_H

#include "leangl_types.h"
#include <stdint.h>

/** 脏区表容量；表满后新区域并入最后一个矩形。 */
#define LEANGL_DIRTY_MAX 8

/**
 * @file    leangl_dirty.h
 * @brief   脏区管理：记录下一次刷新需要重绘的屏幕区域。
 *
 * 坐标均为屏幕绝对坐标，闭区间。添加时会按屏幕尺寸裁剪，相交的区域会合并，
 * 因此表中最多同时存在 LEANGL_DIRTY_MAX 个互不相交的矩形。
 */

/** @brief 清空所有脏区。 */
void LEANGL_Dirty_Clear(void);

/**
 * @brief 清除屏幕行区间 [y1, y2] 内的脏标记。
 *
 * 供刷新模块在本条像素带真正发送成功后逐块调用：只抹掉已经刷过的行，
 * 扫描期间新产生的脏标记（无论在已刷行还是未刷行）都不会被误清。
 *
 * 与区间部分相交的矩形会被裁剪；跨过整个区间的矩形会被拆成上下两块
 * （脏区表满时退化为整块保留，宁可下轮重绘也不丢内容）。
 *
 * @param y1 起始行（屏幕纵坐标，含）。
 * @param y2 结束行（屏幕纵坐标，含）。
 */
void LEANGL_Dirty_Clear_Range(uint16_t y1, uint16_t y2);

/**
 * @brief 标记一块屏幕区域需要重绘。
 *
 * 坐标会自动排序并裁剪到屏幕内；与已有脏区相交时合并。
 *
 * @param x1 左边界（含）。
 * @param y1 上边界（含）。
 * @param x2 右边界（含）。
 * @param y2 下边界（含）。
 */
void LEANGL_Dirty_Add(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

/** @brief 标记一个矩形需要重绘。 */
void LEANGL_Dirty_Add_Rect(const LEANGL_Rect_t *rect);

/** @brief 当前脏区数量。 */
uint8_t LEANGL_Dirty_Count(void);

/**
 * @brief 取第 index 个脏区。
 *
 * @return 指向脏区矩形的指针；index 越界或没有脏区时返回 0。
 */
const LEANGL_Rect_t *LEANGL_Dirty_Get(uint8_t index);

/**
 * @brief 判断屏幕行区间 [y1, y2] 是否与任一脏区相交。
 *
 * @return 1 表示相交，0 表示不相交。
 */
uint8_t LEANGL_Dirty_Intersects(uint16_t y1, uint16_t y2);

/** @brief 是否没有脏区。 */
uint8_t LEANGL_Dirty_Is_Empty(void);

#endif
