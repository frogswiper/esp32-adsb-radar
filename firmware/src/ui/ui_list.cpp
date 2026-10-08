#include "ui_list.h"
#include "ui_main.h"
#include "ui_radar.h"
#include "ui_detail.h"
#include "../board_config.h"
#include "../settings.h"
#include "../adsb.h"
#include "../net_task.h"
#include <Arduino.h>

#define MAX_ROWS 60
static lv_obj_t* g_hdr = nullptr;
static lv_obj_t* g_cont = nullptr;
static lv_obj_t* g_rows[MAX_ROWS] = {};
static char      g_row_hex[MAX_ROWS][8];

static void row_cb(lv_event_t* e) {
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    if (!g_row_hex[k][0]) return;
    ui_radar_select(g_row_hex[k]);
    ui_detail_show(g_row_hex[k]);
    ui_main_goto_tab(PAGE_DETAIL);
}

void ui_list_build(lv_obj_t* parent) {
    g_hdr = lv_label_create(parent);
    lv_obj_set_pos(g_hdr, 6, 4);
    lv_obj_set_width(g_hdr, DISPLAY_WIDTH - 12);
    lv_label_set_long_mode(g_hdr, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(g_hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_hdr, lv_color_hex(0x3DF25A), 0);
    lv_label_set_text(g_hdr, "Aircraft by distance");

    g_cont = lv_obj_create(parent);
    lv_obj_set_size(g_cont, DISPLAY_WIDTH, CONTENT_HEIGHT - 36);
    lv_obj_set_pos(g_cont, 0, 36);
    lv_obj_set_style_bg_color(g_cont, lv_color_black(), 0);
    lv_obj_set_style_border_width(g_cont, 0, 0);
    lv_obj_set_style_pad_all(g_cont, 0, 0);
    lv_obj_set_style_pad_row(g_cont, 1, 0);
    lv_obj_set_style_radius(g_cont, 0, 0);
    lv_obj_set_flex_flow(g_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_cont, LV_DIR_VER);
    lv_obj_set_style_bg_color(g_cont, lv_color_hex(0x1FA84A), LV_PART_SCROLLBAR);

    for (int k = 0; k < MAX_ROWS; k++) {
        lv_obj_t* row = lv_obj_create(g_cont);
        g_rows[k] = row;
        lv_obj_set_size(row, DISPLAY_WIDTH, 34);
        lv_obj_set_style_bg_color(row, (k % 2) ? lv_color_hex(0x04120A) : lv_color_black(), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_left(row, 4, 0);
        lv_obj_set_scroll_dir(row, LV_DIR_NONE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void*)(intptr_t)k);
        lv_obj_t* l1 = lv_label_create(row);
        lv_obj_set_pos(l1, 0, 1);
        lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l1, lv_color_hex(0x3DF25A), 0);
        lv_obj_t* l2 = lv_label_create(row);
        lv_obj_set_pos(l2, 0, 19);
        lv_obj_set_style_text_font(l2, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(l2, lv_color_hex(0x1E9A45), 0);
        lv_obj_set_width(l1, DISPLAY_WIDTH - 8); lv_label_set_long_mode(l1, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(l2, DISPLAY_WIDTH - 8); lv_label_set_long_mode(l2, LV_LABEL_LONG_CLIP);
    }
}

void ui_list_refresh() {
    const Settings& s = settings_get();
    bool av = s.units == UNITS_AVIATION;
    adsb_lock();
    Aircraft* ac = adsb_list(); int n = adsb_count();
    const AdsbStats& st = adsb_stats();
    // sort indices by distance
    int idx[MAX_AIRCRAFT]; int m = 0;
    for (int i = 0; i < n; i++) {
        if (!ac[i].hex[0]) continue;
        if (!adsb_visible(ac[i])) continue;
        idx[m++] = i;
    }
    for (int i = 1; i < m; i++) { int v = idx[i], j = i - 1; while (j >= 0 && ac[idx[j]].dist_km > ac[v].dist_km) { idx[j + 1] = idx[j]; j--; } idx[j + 1] = v; }

    char h[96];
    uint32_t age = st.last_ok_ms ? (millis() - st.last_ok_ms) / 1000 : 0;
    if (m) {
        const Aircraft& hi = ac[st.highest >= 0 ? st.highest : idx[0]];
        const Aircraft& fa = ac[st.fastest >= 0 ? st.fastest : idx[0]];
        snprintf(h, sizeof(h), "%d aircraft within %d %s, %lus ago.  Highest %s %d %s, fastest %s %d %s",
                 m, (int)(av ? s.range_km / 1.852f : s.range_km), av ? "nm" : "km", (unsigned long)age,
                 hi.flight[0] ? hi.flight : hi.hex, av ? (int)hi.alt_ft : (int)lroundf(hi.alt_ft * 0.3048f), av ? "ft" : "m",
                 fa.flight[0] ? fa.flight : fa.hex, av ? fa.gs_kt : (int)(fa.gs_kt * 1.852f), av ? "kt" : "km/h");
    } else {
        char stt[96]; net_status(stt, sizeof(stt));
        snprintf(h, sizeof(h), "No aircraft.  %s", stt);
    }
    lv_label_set_text(g_hdr, h);

    for (int k = 0; k < MAX_ROWS; k++) {
        lv_obj_t* row = g_rows[k];
        if (k >= m) { lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN); g_row_hex[k][0] = 0; continue; }
        const Aircraft& a = ac[idx[k]];
        strlcpy(g_row_hex[k], a.hex, 8);
        char l1[48], l2[64];
        snprintf(l1, sizeof(l1), "%s  %s  %s", a.flight[0] ? a.flight : a.hex, a.type[0] ? a.type : "----", a.reg);
        const char* climb = (a.vr_fpm > 300) ? " " LV_SYMBOL_UP : (a.vr_fpm < -300) ? " " LV_SYMBOL_DOWN : "";
        if (a.on_ground)
            snprintf(l2, sizeof(l2), "%.1f %s  %03.0f\xc2\xb0  on ground  sq %s", av ? a.dist_km / 1.852f : a.dist_km, av ? "nm" : "km", a.bearing, a.squawk);
        else if (av)
            snprintf(l2, sizeof(l2), "%.1f nm  %03.0f\xc2\xb0  FL%03d%s  %d kt  trk %03d  sq %s%s", a.dist_km / 1.852f, a.bearing, (int)(a.alt_ft / 100), climb, a.gs_kt, a.track < 0 ? 0 : a.track, a.squawk, a.military ? "  MIL" : "");
        else
            snprintf(l2, sizeof(l2), "%.1f km  %03.0f\xc2\xb0  %d m%s  %d km/h  trk %03d  sq %s%s", a.dist_km, a.bearing, (int)lroundf(a.alt_ft * 0.3048f), climb, (int)(a.gs_kt * 1.852f), a.track < 0 ? 0 : a.track, a.squawk, a.military ? "  MIL" : "");
        if (a.emergency) snprintf(l2, sizeof(l2), "EMERGENCY  squawk %s  %.1f %s  %03.0f\xc2\xb0", a.squawk, av ? a.dist_km / 1.852f : a.dist_km, av ? "nm" : "km", a.bearing);
        lv_label_set_text(lv_obj_get_child(row, 0), l1);
        lv_label_set_text(lv_obj_get_child(row, 1), l2);
        lv_obj_set_style_text_color(lv_obj_get_child(row, 0),
            a.emergency ? lv_color_hex(0xFF3B3B) : (a.watch || a.notable) ? lv_color_hex(0xFFE44D) : (a.military && s.highlight_mil) ? lv_color_hex(0xFFA030) : a.cls == CLS_LIGHT ? lv_color_hex(0x3EC9B0) : a.cls == CLS_HELI ? lv_color_hex(0xB0FF40) : lv_color_hex(0x3DF25A), 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
    }
    adsb_unlock();
}
