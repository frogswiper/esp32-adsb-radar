/**
 * src/lvgl/lvgl.h — shim so LovyanGFX's __has_include("lvgl/lvgl.h") check
 * succeeds (via -I src), causing it to skip its internal font headers and
 * avoid lv_font_fmt_txt_* redefinition conflicts with the real LVGL library.
 */
#pragma once
#include <lvgl.h>
