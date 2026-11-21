// The board-support package's panel timing, which it gives only as a C initializer macro (its
// nested designators are not C++): display.cpp takes it from here, so the BSP stays the one
// source of the porches and sync widths.
#include "bsp/esp32_s31_korvo_1.h"
#include "esp_lcd_panel_rgb.h"

esp_lcd_rgb_timing_t korvo1_bsp_panel_timing(void)
{
    return (esp_lcd_rgb_timing_t)BSP_DISPLAY_PANEL_RGB_TIMING();
}
