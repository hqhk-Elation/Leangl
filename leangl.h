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

#ifndef LEANGL_H
#define LEANGL_H

/**
 * @file    leangl.h
 * @brief   LeanGL 公共接口总入口。
 *
 * 典型初始化顺序：
 *   1. LEANGL_Set_Screen() 设置屏幕分辨率；
 *   2. LEANGL_Set_Flush_CB() / LEANGL_Set_Tick_CB() 登记硬件回调；
 *   3. LEANGL_Set_Font() 注册字体（否则文本不会绘制）；
 *   4. LEANGL_Buffer_Set() 提供绘图缓冲区；
 *   5. 创建控件并调用 LEANGL_Widget_Add()；
 *   6. 主循环反复调用 LEANGL_Handler()。
 */

#include "leangl_conf.h"

#include "leangl_port.h"
#include "leangl_types.h"
#include "leangl_font.h"
#include "leangl_buffer.h"
#include "leangl_dirty.h"
#include "leangl_flush.h"
#include "leangl_widget.h"
#include "leangl_label.h"
#include "leangl_button.h"
#include "leangl_list.h"
#include "leangl_scroll_label.h"
#include "leangl_draw_char.h"
#include "leangl_draw_shape.h"
#include "leangl_draw_pixel.h"
#include "leangl_colors.h"

#endif