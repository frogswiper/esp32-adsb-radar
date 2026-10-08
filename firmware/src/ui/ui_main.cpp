#include "ui_main.h"
#include "ui_radar.h"
#include "ui_list.h"
#include "ui_info.h"
#include "ui_settings.h"
#include "ui_detail.h"
#include "ui_stats.h"
#include "../board_config.h"
#include <initializer_list>

static lv_obj_t* g_tabview = nullptr;
static lv_obj_t* g_nav     = nullptr;
static lv_obj_t* g_nav_btns[5] = {};
static int       g_active  = 0;
static bool      g_fetch_req = false;

static const char* TAB_ICONS[]  = {LV_SYMBOL_GPS, LV_SYMBOL_LIST, LV_SYMBOL_IMAGE, LV_SYMBOL_CHARGE, LV_SYMBOL_SETTINGS};
static const char* TAB_LABELS[] = {"Radar", "List", "Stats", "Info", "Settings"};
#define C_ACTIVE   lv_color_hex(0x3DF25A)
#define C_INACTIVE lv_color_hex(0x1E9A45)

static void update_nav() {
    for (int i = 0; i < 5; i++) {
        lv_color_t col = (i == g_active) ? C_ACTIVE : C_INACTIVE;
        for (uint32_t c = 0; c < lv_obj_get_child_cnt(g_nav_btns[i]); c++)
            lv_obj_set_style_text_color(lv_obj_get_child(g_nav_btns[i], c), col, 0);
    }
    if (g_active == 0) lv_obj_add_flag(g_nav, LV_OBJ_FLAG_HIDDEN);
    else               lv_obj_clear_flag(g_nav, LV_OBJ_FLAG_HIDDEN);
    ui_radar_set_active(g_active == 0);
}

static void nav_btn_cb(lv_event_t* e) {
    ui_main_goto_tab((int)(intptr_t)lv_event_get_user_data(e));
}

void ui_main_init() {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    g_tabview = lv_tabview_create(scr, LV_DIR_TOP, 0);
    lv_obj_set_size(g_tabview, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_pos(g_tabview, 0, 0);
    lv_obj_set_style_bg_color(g_tabview, lv_color_black(), 0);
    lv_obj_set_style_border_width(g_tabview, 0, 0);
    lv_obj_set_style_pad_all(g_tabview, 0, 0);
    lv_obj_clear_flag(lv_tabview_get_content(g_tabview), LV_OBJ_FLAG_SCROLLABLE);  // no swipe between pages

    lv_obj_t* t0 = lv_tabview_add_tab(g_tabview, "Radar");
    lv_obj_t* t1 = lv_tabview_add_tab(g_tabview, "List");
    lv_obj_t* t2 = lv_tabview_add_tab(g_tabview, "Info");
    lv_obj_t* t3 = lv_tabview_add_tab(g_tabview, "Settings");
    lv_obj_t* t4 = lv_tabview_add_tab(g_tabview, "Detail");
    lv_obj_t* ts = lv_tabview_add_tab(g_tabview, "Stats");   // index 5 in tabview; mapped below
    for (lv_obj_t* t : {t0, t1, t2, t3, t4, ts}) {
        lv_obj_set_style_pad_all(t, 0, 0);
        lv_obj_set_style_bg_color(t, lv_color_black(), 0);
    }
    // pages 1-3 leave room for the nav bar
    for (lv_obj_t* t : {t1, t2, t3, t4, ts}) lv_obj_set_style_pad_bottom(t, TAB_BAR_HEIGHT, 0);

    ui_radar_build(t0);
    ui_list_build(t1);
    ui_info_build(t2);
    ui_settings_build(t3);
    ui_detail_build(t4);
    ui_stats_build(ts);

    // bottom navigation bar (hidden on the radar page)
    g_nav = lv_obj_create(scr);
    lv_obj_set_size(g_nav, DISPLAY_WIDTH, TAB_BAR_HEIGHT);
    lv_obj_set_pos(g_nav, 0, CONTENT_HEIGHT);
    lv_obj_set_style_bg_color(g_nav, lv_color_hex(0x050E08), 0);
    lv_obj_set_style_border_color(g_nav, lv_color_hex(0x0E5A27), 0);
    lv_obj_set_style_border_width(g_nav, 1, 0);
    lv_obj_set_style_border_side(g_nav, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(g_nav, 0, 0);
    lv_obj_set_style_pad_all(g_nav, 0, 0);
    lv_obj_set_flex_flow(g_nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_nav, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(g_nav, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 5; i++) {
        lv_obj_t* btn = lv_btn_create(g_nav);
        g_nav_btns[i] = btn;
        lv_obj_set_size(btn, DISPLAY_WIDTH / 5, TAB_BAR_HEIGHT);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(btn, 2, 0);
        lv_obj_t* icon = lv_label_create(btn);
        lv_label_set_text(icon, TAB_ICONS[i]);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, TAB_LABELS[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_add_event_cb(btn, nav_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    g_active = 0;
    update_nav();
}

// page index → tabview index (Stats page was added last in the tabview)
static int tv_index(int page) {
    switch (page) { case PAGE_RADAR: return 0; case PAGE_LIST: return 1; case PAGE_STATS: return 5; case PAGE_INFO: return 2; case PAGE_SETTINGS: return 3; case PAGE_DETAIL: return 4; }
    return 0;
}
void ui_main_goto_tab(int idx) {
    if (idx < 0 || idx > 5) return;
    g_active = idx;
    lv_tabview_set_act(g_tabview, tv_index(idx), LV_ANIM_OFF);
    update_nav();
    if (idx == PAGE_LIST)   ui_list_refresh();
    if (idx == PAGE_STATS)  ui_stats_refresh();
    if (idx == PAGE_INFO)   ui_info_refresh();
    if (idx == PAGE_DETAIL) ui_detail_refresh();
}
int  ui_main_active_tab() { return g_active; }
void ui_main_request_fetch() { g_fetch_req = true; }
bool ui_main_consume_fetch_request() { bool r = g_fetch_req; g_fetch_req = false; return r; }
