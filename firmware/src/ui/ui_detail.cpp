#include "ui_detail.h"
#include "ui_main.h"
#include "ui_radar.h"
#include "../board_config.h"
#include "../settings.h"
#include "../adsb.h"
#include <Arduino.h>

#define C_HDR  lv_color_hex(0x3DF25A)
#define C_TXT  lv_color_hex(0x33D650)
#define C_DIM  lv_color_hex(0x1E9A45)
#define C_LINE lv_color_hex(0x1FA84A)
#define C_WARN lv_color_hex(0xFFE44D)
#define C_EMG  lv_color_hex(0xFF3B3B)

static char      g_hex[8] = "";
static lv_obj_t *g_title, *g_sub, *g_body, *g_spark, *g_spark_lbl, *g_sq, *g_cpa;
static lv_color_t* g_spark_buf = nullptr;
static const int SPARK_W = 252, SPARK_H = 56;

static void back_cb(lv_event_t*)  { ui_main_goto_tab(PAGE_RADAR); }
static void radar_cb(lv_event_t*) { ui_radar_select(g_hex[0] ? g_hex : nullptr); ui_main_goto_tab(PAGE_RADAR); }

static lv_obj_t* label(lv_obj_t* p, const lv_font_t* f, lv_color_t c) {
    lv_obj_t* l = lv_label_create(p);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}
static lv_obj_t* button(lv_obj_t* p, const char* t, lv_event_cb_t cb, int w) {
    lv_obj_t* b = lv_btn_create(p);
    lv_obj_set_size(b, w, 34);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x07200F), 0);
    lv_obj_set_style_border_color(b, C_LINE, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, C_HDR, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

void ui_detail_build(lv_obj_t* parent) {
    lv_obj_t* col = lv_obj_create(parent);
    lv_obj_set_size(col, DISPLAY_WIDTH, CONTENT_HEIGHT);
    lv_obj_set_pos(col, 0, 0);
    lv_obj_set_style_bg_color(col, lv_color_black(), 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_radius(col, 0, 0);
    lv_obj_set_style_pad_all(col, 8, 0);
    lv_obj_set_style_pad_row(col, 6, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);
    lv_obj_set_style_bg_color(col, C_LINE, LV_PART_SCROLLBAR);

    lv_obj_t* row = lv_obj_create(col);
    lv_obj_set_size(row, LV_PCT(100), 34);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    button(row, LV_SYMBOL_LEFT " Back", back_cb, 90);
    button(row, LV_SYMBOL_GPS " Show on radar", radar_cb, 150);

    g_title = label(col, &lv_font_montserrat_24, C_HDR);
    g_sub   = label(col, &lv_font_montserrat_14, C_TXT);
    g_body  = label(col, &lv_font_montserrat_12, C_TXT);

    g_spark_lbl = label(col, &lv_font_montserrat_12, C_DIM);
    g_spark_buf = (lv_color_t*)heap_caps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(SPARK_W, SPARK_H), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_spark = lv_canvas_create(col);
    lv_canvas_set_buffer(g_spark, g_spark_buf, SPARK_W, SPARK_H, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(g_spark, lv_color_black(), LV_OPA_COVER);

    g_cpa = label(col, &lv_font_montserrat_12, C_TXT);
    g_sq  = label(col, &lv_font_montserrat_12, C_DIM);
}

void ui_detail_show(const char* hex) {
    if (hex) strlcpy(g_hex, hex, sizeof(g_hex)); else g_hex[0] = 0;
    ui_detail_refresh();
}

static void draw_spark(const Aircraft& a) {
    lv_canvas_fill_bg(g_spark, lv_color_black(), LV_OPA_COVER);
    int n = a.trail_n; int vals[TRAIL_LEN + 1]; int m = 0;
    for (int j = 0; j < n; j++) vals[m++] = a.trail_alt[(a.trail_head - a.trail_n + j + TRAIL_LEN) % TRAIL_LEN] * 10;
    vals[m++] = a.alt_ft;
    int lo = vals[0], hi = vals[0];
    for (int i = 1; i < m; i++) { if (vals[i] < lo) lo = vals[i]; if (vals[i] > hi) hi = vals[i]; }
    if (hi - lo < 200) { hi += 100; lo -= 100; }
    lv_draw_line_dsc_t ld; lv_draw_line_dsc_init(&ld); ld.color = C_DIM; ld.width = 1; ld.dash_width = 2; ld.dash_gap = 3;
    lv_point_t base[2] = {{0, SPARK_H - 1}, {SPARK_W - 1, SPARK_H - 1}}; lv_canvas_draw_line(g_spark, base, 2, &ld);
    lv_point_t top[2]  = {{0, 0}, {SPARK_W - 1, 0}}; lv_canvas_draw_line(g_spark, top, 2, &ld);
    ld.color = C_HDR; ld.width = 2; ld.dash_width = 0;
    lv_point_t pts[TRAIL_LEN + 1];
    for (int i = 0; i < m; i++) {
        pts[i].x = (m > 1) ? (lv_coord_t)(i * (SPARK_W - 1) / (m - 1)) : SPARK_W / 2;
        pts[i].y = (lv_coord_t)(SPARK_H - 2 - (int)((long)(vals[i] - lo) * (SPARK_H - 4) / (hi - lo)));
    }
    if (m > 1) lv_canvas_draw_line(g_spark, pts, m, &ld);
    lv_draw_rect_dsc_t rd; lv_draw_rect_dsc_init(&rd); rd.bg_color = C_WARN; rd.radius = LV_RADIUS_CIRCLE; rd.border_width = 0;
    lv_canvas_draw_rect(g_spark, pts[m - 1].x - 3, pts[m - 1].y - 3, 7, 7, &rd);
    const Settings& s = settings_get();
    char t[64];
    if (s.units == UNITS_AVIATION) snprintf(t, sizeof(t), "Altitude trend  %d - %d ft  (%d samples)", lo, hi, m);
    else snprintf(t, sizeof(t), "Altitude trend  %d - %d m  (%d samples)", (int)lroundf(lo * 0.3048f), (int)lroundf(hi * 0.3048f), m);
    lv_label_set_text(g_spark_lbl, t);
    lv_obj_invalidate(g_spark);
}

void ui_detail_refresh() {
    if (!g_title) return;
    const Settings& s = settings_get();
    bool av = s.units == UNITS_AVIATION;
    adsb_lock();
    int idx = g_hex[0] ? adsb_find_hex(g_hex) : -1;
    if (idx < 0) {
        adsb_unlock();
        lv_label_set_text(g_title, g_hex[0] ? g_hex : "No aircraft");
        lv_label_set_text(g_sub, g_hex[0] ? "Out of range / no longer tracked" : "Tap an aircraft row on the radar or list");
        lv_label_set_text(g_body, ""); lv_label_set_text(g_spark_lbl, ""); lv_label_set_text(g_cpa, ""); lv_label_set_text(g_sq, "");
        lv_canvas_fill_bg(g_spark, lv_color_black(), LV_OPA_COVER);
        return;
    }
    Aircraft a = adsb_list()[idx];   // copy, then release the lock
    adsb_unlock();

    char t[200];
    lv_label_set_text(g_title, a.flight[0] ? a.flight : a.hex);
    lv_obj_set_style_text_color(g_title, a.emergency ? C_EMG : (a.alert || a.watch || a.notable) ? C_WARN : C_HDR, 0);
    const char* airline = adsb_airline_name(a.flight);
    const char* tname = adsb_type_name(a.type);
    snprintf(t, sizeof(t), "%s%s%s%s%s", airline[0] ? airline : (a.military ? "Military" : a.cls == CLS_HELI ? "Helicopter" : "Private / GA"),
             tname[0] || a.type[0] ? "  -  " : "", tname[0] ? tname : a.type, a.type[0] && tname[0] ? " (" : "", a.type[0] && tname[0] ? a.type : "");
    if (a.type[0] && tname[0]) strlcat(t, ")", sizeof(t));
    lv_label_set_text(g_sub, t);

    char route[160];
    if (a.route[0]) {
        char org[4] = {0}, dst[4] = {0}; strncpy(org, a.route, 3);
        size_t rl = strlen(a.route); if (rl >= 3) strncpy(dst, a.route + rl - 3, 3);
        snprintf(route, sizeof(route), "From %s %s\n       %s\nTo    %s %s\n       %s", org, a.org_city, a.org_name, dst, a.dest_city, a.dest_name);
    } else snprintf(route, sizeof(route), "Route: %s", a.route_tried ? "not in route database" : "looking up...");

    const char* cat = adsb_category_name(a.category);
    char body[520];
    if (a.on_ground)
        snprintf(body, sizeof(body), "%s\n\nReg %s   ICAO hex %s%s\n%s%s\nOn ground   %.1f %s   bearing %03.0f\xc2\xb0",
                 route, a.reg[0] ? a.reg : "-", a.hex, a.military ? "   MILITARY" : "", cat, cat[0] ? "\n" : "",
                 av ? a.dist_km / 1.852f : a.dist_km, av ? "nm" : "km", a.bearing);
    else if (av)
        snprintf(body, sizeof(body), "%s\n\nReg %s   ICAO hex %s%s\n%s%s\nAltitude %d ft   %+d ft/min\nSpeed %d kt   track %03d\xc2\xb0\nDistance %.1f nm   bearing %03.0f\xc2\xb0",
                 route, a.reg[0] ? a.reg : "-", a.hex, a.military ? "   MILITARY" : "", cat, cat[0] ? "\n" : "",
                 (int)a.alt_ft, a.vr_fpm, a.gs_kt, a.track < 0 ? 0 : a.track, a.dist_km / 1.852f, a.bearing);
    else
        snprintf(body, sizeof(body), "%s\n\nReg %s   ICAO hex %s%s\n%s%s\nAltitude %d m   %+.1f m/s\nSpeed %d km/h   track %03d\xc2\xb0\nDistance %.1f km   bearing %03.0f\xc2\xb0",
                 route, a.reg[0] ? a.reg : "-", a.hex, a.military ? "   MILITARY" : "", cat, cat[0] ? "\n" : "",
                 (int)lroundf(a.alt_ft * 0.3048f), a.vr_fpm * 0.00508f, (int)lroundf(a.gs_kt * 1.852f), a.track < 0 ? 0 : a.track, a.dist_km, a.bearing);
    lv_label_set_text(g_body, body);

    draw_spark(a);

    // spotter line: where to look + flyover prediction
    char look[64];
    if (a.on_ground) look[0] = 0;
    else snprintf(look, sizeof(look), LV_SYMBOL_EYE_OPEN "  Look %s (%03.0f\xc2\xb0), %.0f\xc2\xb0 above the horizon", adsb_compass16(a.bearing), a.bearing, a.elev_deg);
    if (a.alert)
        snprintf(t, sizeof(t), "%s\n" LV_SYMBOL_WARNING "  OVERHEAD  passes you in ~%.0f min at %.1f km", look, a.cpa_min, a.cpa_km);
    else if (!a.on_ground && a.cpa_min > 0.5f && a.cpa_km < a.dist_km * 0.9f)
        snprintf(t, sizeof(t), "%s\nPasses you in ~%.0f min at %.1f km", look, a.cpa_min, a.cpa_km);
    else snprintf(t, sizeof(t), "%s", look);
    if (a.watch)   strlcat(t, "\n" LV_SYMBOL_BELL "  On your watchlist", sizeof(t));
    if (a.notable) strlcat(t, "\n" LV_SYMBOL_BELL "  Notable aircraft", sizeof(t));
    lv_label_set_text(g_cpa, t);
    lv_obj_set_style_text_color(g_cpa, (a.alert || a.watch || a.notable) ? C_WARN : C_TXT, 0);

    snprintf(t, sizeof(t), "Squawk %s  -  %s%s   seen %lu min", a.squawk[0] ? a.squawk : "-", adsb_squawk_meaning(a.squawk),
             a.emergency ? "  EMERGENCY" : "", (unsigned long)((millis() - a.first_seen_ms) / 60000));
    lv_label_set_text(g_sq, t);
    lv_obj_set_style_text_color(g_sq, a.emergency ? C_EMG : C_DIM, 0);
}
