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

#include "include/leangl_port.h"
#include "include/leangl_types.h"
#include "include/leangl_font.h"
#include "include/leangl_buffer.h"
#include "include/leangl_dirty.h"
#include "include/leangl_flush.h"
#include "include/leangl_widget.h"
#include "include/leangl_label.h"
#include "include/leangl_button.h"
#include "include/leangl_list.h"
#include "include/leangl_scroll_label.h"
#include "include/leangl_draw_char.h"
#include "include/leangl_draw_shape.h"
#include "include/leangl_draw_pixel.h"
#include "include/leangl_colors.h"

#endif