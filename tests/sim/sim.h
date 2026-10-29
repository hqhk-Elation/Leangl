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

#ifndef LEANGL_SIM_H
#define LEANGL_SIM_H

/**
 * @file    sim.h
 * @brief   Win32 屏幕模拟器的对外接口。
 *
 * 模拟器分成两部分：
 *   - 引擎（sim_win32.c）：窗口、显存 DIB、字库、刷新与 DMA 调度、
 *     以及字库读取/光栅化/刷屏的耗时模型（读 sim_config.ini）；
 *   - 界面（sim_demo.c）：实现 leangl_sim_ui()，负责创建控件。
 *
 * 写自己的界面时，照着 sim_demo.c 实现一个 leangl_sim_ui() 即可，
 * 引擎不需要改动。控件坐标使用屏幕绝对坐标，范围是 build() 收到的
 * width / height，与真实屏幕一致。
 *
 * 与引擎的约定：
 *   - build() 在 LEANGL_Set_Screen() 和 LEANGL_Buffer_Set() 之后调用一次；
 *   - update() 每帧调用一次，可用来刷新周期性变化的内容（不要在里面画图）；
 *   - pointer() 只在鼠标事件时调用，pressed 为 1 表示按下或拖动中，0 表示松开；
 *   - key() 在按下可打印键时调用，ch 是转成小写的 ASCII 字符（'a'~'z'、'0'~'9' 等）。
 *     ESC 被引擎保留用于退出，不会转给界面。
 */

#include <stdint.h>

/** @brief 界面描述。四个回调都可以为 0。 */
typedef struct LEANGL_Sim_UI {
    const char *name; /**< 界面名称，显示在控制台日志里。 */

    /**
     * @brief 创建界面。
     *
     * @param width  屏幕宽度（像素）。
     * @param height 屏幕高度（像素）。
     */
    void (*build)(uint16_t width, uint16_t height);

    /**
     * @brief 每帧回调。
     *
     * @param ms 自启动以来的毫秒数（取自 tick_cb）。
     */
    void (*update)(uint32_t ms);

    /**
     * @brief 鼠标事件。
     *
     * @param x       屏幕横坐标。
     * @param y       屏幕纵坐标。
     * @param pressed 1 表示按下或拖动中；0 表示松开。
     */
    void (*pointer)(int x, int y, int pressed);

    /**
     * @brief 键盘事件。
     *
     * 只在按下可打印键时触发，ch 已经转成小写 ASCII（'a'~'z'、'0'~'9' 等）。
     * 控制键（方向键、功能键）与 ESC 不会传进来。
     *
     * @param ch 字符（小写 ASCII）。
     */
    void (*key)(int ch);
} LEANGL_Sim_UI_t;

/**
 * @brief 取得界面描述。
 *
 * 由界面实现文件提供。返回 0 表示不带界面（模拟器仍可正常刷新）。
 */
const LEANGL_Sim_UI_t *leangl_sim_ui(void);

/* ---------------- 引擎信息 ---------------- */

/** @brief 当前模拟的屏幕宽度（像素）。 */
uint16_t LEANGL_Sim_Width(void);

/** @brief 当前模拟的屏幕高度（像素）。 */
uint16_t LEANGL_Sim_Height(void);

/** @brief 每条像素带的行数（等于 buffer.lines）。 */
uint16_t LEANGL_Sim_Band_Lines(void);

/** @brief 屏幕被切分成的像素带数量。 */
uint16_t LEANGL_Sim_Band_Count(void);

/** @brief flush_cb 累计被调用的次数。 */
uint32_t LEANGL_Sim_Band_Flushes(void);

/** @brief 刷新模式："blocking" 或 "async"。 */
const char *LEANGL_Sim_Mode(void);

/* ---------------- 引擎操作 ---------------- */

/** @brief 把整个屏幕标脏，请求下一次重绘。 */
void LEANGL_Sim_Refresh(void);

/** @brief 请求退出主循环。 */
void LEANGL_Sim_Quit(void);

#endif
