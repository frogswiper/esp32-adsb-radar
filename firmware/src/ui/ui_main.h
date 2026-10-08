#pragma once
#include <lvgl.h>

// Pages: 0 = Radar (full screen, no nav bar), 1 = List, 2 = Stats, 3 = Info, 4 = Settings, 5 = Flight detail (no nav button)
#define PAGE_RADAR 0
#define PAGE_LIST 1
#define PAGE_STATS 2
#define PAGE_INFO 3
#define PAGE_SETTINGS 4
#define PAGE_DETAIL 5
void ui_main_init();
void ui_main_goto_tab(int idx);
int  ui_main_active_tab();
void ui_main_request_fetch();          // ask main loop for an immediate aircraft poll
bool ui_main_consume_fetch_request();
