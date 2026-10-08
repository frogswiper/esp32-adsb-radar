#pragma once
#include <lvgl.h>
void ui_settings_build(lv_obj_t* parent);
void ui_settings_set_status(const char* msg);
void ui_settings_cities_loaded();     // net task delivered a city list
void ui_settings_location_changed();
void ui_settings_debug(char what);          // serial debug: K=keyboard, O=open country dropdown  // refresh "current location" line
