#pragma once
#include <lvgl.h>

void ui_radar_build(lv_obj_t* parent);
void ui_radar_notify_data();        // new aircraft data arrived → redraw + bottom panel
void ui_radar_notify_weather();     // temperature / wind line
void ui_radar_range_changed();      // settings range/units changed → rebuild background
void ui_radar_select(const char* hex);   // highlight aircraft (nullptr = none)
void ui_radar_set_active(bool active);   // page shown/hidden (pauses sweep timer)
