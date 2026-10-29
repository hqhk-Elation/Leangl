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

#include "leangl_port.h"
#include <stdint.h>

LEANGL_Port_t leangl_port;

void LEANGL_Set_Screen(uint32_t width, uint32_t height) {
    leangl_port.screen.width = width;
    leangl_port.screen.height = height;
}

void LEANGL_Set_Flush_CB(void (*flush_cb)(uint16_t y, uint16_t h,
                                          const uint8_t *data)) {
    leangl_port.flush_cb = flush_cb;
}

void LEANGL_Set_Tick_CB(uint32_t (*tick_cb)(void)) {
    leangl_port.tick_cb = tick_cb;
}
