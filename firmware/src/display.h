#pragma once
// Always include lvgl before LovyanGFX to avoid lv_font header conflicts
#include <lvgl.h>  // NOLINT

/**
 * Initialise the display (LovyanGFX RGB panel + LVGL) and
 * touch input (GT911).  Call once in setup() before any LVGL work.
 */
void display_init();

/** Raw brightness 0-255 via LEDC PWM on the backlight pin. */
void display_set_brightness(uint8_t value);
