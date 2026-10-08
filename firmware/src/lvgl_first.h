/**
 * lvgl_first.h — force-included before every translation unit via build_flags.
 *
 * LovyanGFX's lgfx/v1/lvgl.h checks:
 *   #if LVGL_VERSION_MAJOR   ← defined by lvgl.h as 8
 * If truthy it skips including its own bundled lv_font headers, preventing
 * "conflicting declaration" errors when both libraries are linked.
 */
#pragma once
#include <lvgl.h>
