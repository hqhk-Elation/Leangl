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
