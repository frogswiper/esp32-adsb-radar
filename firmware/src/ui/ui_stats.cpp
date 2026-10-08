#include "ui_stats.h"
#include "../board_config.h"
#include "../settings.h"
#include "../stats.h"
#include "../adsb.h"
#include "../extras.h"
#include <Arduino.h>

#define C_HDR  lv_color_hex(0x3DF25A)
#define C_TXT  lv_color_hex(0x33D650)
#define C_DIM  lv_color_hex(0x1E9A45)
#define C_LINE lv_color_hex(0x1FA84A)

static lv_obj_t *g_records, *g_chart, *g_top, *g_metar;
static lv_color_t* g_chart_buf = nullptr;
static const int CW = 252, CH = 70;

static lv_obj_t* label(lv_obj_t* p, const lv_font_t* f, lv_color_t c) {
    lv_obj_t* l = lv_label_create(p);
    lv_obj_set_width(l, LV_PCT(100)); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, c, 0); lv_label_set_text(l, "");
    return l;
}

void ui_stats_build(lv_obj_t* parent) {
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

    lv_obj_t* h = label(col, &lv_font_montserrat_16, C_HDR); lv_label_set_text(h, "Today");
    g_records = label(col, &lv_font_montserrat_12, C_TXT);
    lv_obj_t* h2 = label(col, &lv_font_montserrat_12, C_DIM); lv_label_set_text(h2, "New aircraft per hour (local time)");
    g_chart_buf = (lv_color_t*)heap_caps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(CW, CH), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_chart = lv_canvas_create(col);
    lv_canvas_set_buffer(g_chart, g_chart_buf, CW, CH, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(g_chart, lv_color_black(), LV_OPA_COVER);
    g_top = label(col, &lv_font_montserrat_12, C_TXT);
    g_metar = label(col, &lv_font_montserrat_12, C_DIM);
}

void ui_stats_refresh() {
    if (!g_records) return;
    const Stats& st = stats_get();
    const Settings& s = settings_get();
    bool av = s.units == UNITS_AVIATION;
    char t[400];
    snprintf(t, sizeof(t),
        "Unique aircraft: %lu     most at once: %u\nHighest: %s  %d %s\nFastest: %s  %d %s\nClosest: %s  %.1f km\nEmergencies: %u   overhead alerts: %u   boots: %lu",
        (unsigned long)st.unique_today, st.max_tracked,
        st.max_alt_who[0] ? st.max_alt_who : "-", av ? (int)st.max_alt_ft : (int)lroundf(st.max_alt_ft * 0.3048f), av ? "ft" : "m",
        st.max_gs_who[0] ? st.max_gs_who : "-", av ? (int)st.max_gs_kt : (int)lroundf(st.max_gs_kt * 1.852f), av ? "kt" : "km/h",
        st.min_dist_who[0] ? st.min_dist_who : "-", st.min_dist_km, st.emergencies, st.alerts, (unsigned long)st.boot_count);
    lv_label_set_text(g_records, t);

    // hourly bar chart
    lv_canvas_fill_bg(g_chart, lv_color_black(), LV_OPA_COVER);
    uint16_t mx = 1; for (int i = 0; i < 24; i++) if (st.hourly[i] > mx) mx = st.hourly[i];
    lv_draw_rect_dsc_t rd; lv_draw_rect_dsc_init(&rd); rd.bg_color = C_LINE; rd.border_width = 0; rd.radius = 1;
    lv_draw_label_dsc_t ld; lv_draw_label_dsc_init(&ld); ld.font = &lv_font_unscii_8; ld.color = C_DIM;
    int bw = CW / 24;
    for (int i = 0; i < 24; i++) {
        int hgt = (int)((CH - 12) * (long)st.hourly[i] / mx);
        if (hgt > 0) lv_canvas_draw_rect(g_chart, i * bw + 1, CH - 12 - hgt, bw - 2, hgt, &rd);
        if (i % 6 == 0) { char hb[4]; snprintf(hb, sizeof(hb), "%02d", i); lv_canvas_draw_text(g_chart, i * bw, CH - 10, 20, &ld, hb); }
    }
    char mxs[8]; snprintf(mxs, sizeof(mxs), "%u", mx); lv_canvas_draw_text(g_chart, CW - 24, 0, 24, &ld, mxs);
    lv_obj_invalidate(g_chart);

    // top airlines / types (sorted copies)
    auto top_text = [&](const TopEntry* src, const char* title, bool airline, size_t& o) {
        TopEntry c[STATS_TOP]; memcpy(c, src, sizeof(c));
        for (int a = 1; a < STATS_TOP; a++) { TopEntry v = c[a]; int b = a - 1; while (b >= 0 && c[b].count < v.count) { c[b + 1] = c[b]; b--; } c[b + 1] = v; }
        o += snprintf(t + o, sizeof(t) - o, "%s", title);
        int shown = 0;
        for (int a = 0; a < STATS_TOP && shown < 6; a++) {
            if (!c[a].key[0] || !c[a].count) continue;
            const char* nm = airline ? adsb_airline_name(c[a].key) : c[a].key;
            o += snprintf(t + o, sizeof(t) - o, "%s%s %u", shown ? ", " : "", nm[0] ? nm : c[a].key, c[a].count);
            shown++;
        }
        if (!shown) o += snprintf(t + o, sizeof(t) - o, "-");
    };
    size_t o = 0; t[0] = 0;
    top_text(st.airlines, "Top airlines: ", true, o);
    o += snprintf(t + o, sizeof(t) - o, "\n");
    top_text(st.types, "Top types: ", false, o);
    lv_label_set_text(g_top, t);

    const MetarState& m = extras_metar();
    if (m.valid) lv_label_set_text(g_metar, m.raw);
    else lv_label_set_text(g_metar, s.metar ? "METAR: waiting..." : "");
}
