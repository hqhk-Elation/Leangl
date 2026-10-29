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

#ifndef LEANGL_INTERNAL_H
#define LEANGL_INTERNAL_H

/**
 * @file    leangl_internal.h
 * @brief   内部头文件：模块之间共享的全局实例。
 *
 * 调用端不要包含本文件。屏幕、缓冲区和回调请分别通过
 * LEANGL_Set_Screen()、LEANGL_Buffer_Set()、LEANGL_Set_*_CB() 配置。
 */

#include "leangl_buffer.h"
#include "leangl_flush.h"
#include "leangl_port.h"

extern LEANGL_Port_t leangl_port;   /**< 端口配置实例 */
extern LEANGL_Buffer_t buffer;      /**< 全局缓冲区实例 */
extern LEANGL_Flush_t leangl_flush; /**< 当前刷新块窗口 */

#endif
