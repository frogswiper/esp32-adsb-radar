#include "ui_radar.h"
#include "ui_main.h"
#include "../board_config.h"
#include "../settings.h"
#include "../adsb.h"
#include "../net_task.h"
#include "../ntp.h"
#include "../map_data.h"
#include "../airports.h"
#include "ui_detail.h"
#include "../extras.h"
#include <Arduino.h>
#include <math.h>

// ── HUD palette ──────────────────────────────────────────────────────────────
#define C_BG       lv_color_black()
#define C_RING     lv_color_hex(0x1FA84A)
#define C_RING_DIM lv_color_hex(0x0E5A27)
#define C_TEXT     lv_color_hex(0x3DF25A)
#define C_TEXT_DIM lv_color_hex(0x1E9A45)
#define C_SWEEP    lv_color_hex(0x27C456)
#define C_AC       lv_color_hex(0x8CFF7A)
#define C_AC_GND   lv_color_hex(0x5E8A6A)
#define C_AC_MIL   lv_color_hex(0xFFA030)
#define C_AC_EMG   lv_color_hex(0xFF3B3B)
#define C_AC_SEL   lv_color_hex(0xFFE44D)
#define C_AC_LIGHT lv_color_hex(0x3EC9B0)
#define C_AC_HELI  lv_color_hex(0xB0FF40)
#define C_MAP      lv_color_hex(0x0E5A27)
#define C_LAKE     lv_color_hex(0x0A4A20)
#define C_BORDER   lv_color_hex(0x0C4A20)

static lv_obj_t*   g_canvas = nullptr;
static lv_color_t* g_buf    = nullptr;     // live canvas buffer (PSRAM)
static lv_color_t* g_bg     = nullptr;     // pre-rendered static background (PSRAM)
static int         g_bg_range = -1;
static uint8_t     g_bg_units = 255;
static float       g_sweep    = 0;
static lv_timer_t* g_timer    = nullptr;
static bool        g_active   = true;
static bool        g_dirty    = true;
static char        g_sel_hex[8] = "";
static bool        g_blink = false;
static uint8_t     g_blink_ticks = 0;

// bottom panel
static lv_obj_t* g_lbl_wx = nullptr;     // left: temperature + condition
static lv_obj_t* g_lbl_wind = nullptr;   // right: wind + gust
static lv_obj_t* g_wx_icon = nullptr;    // 22x22 canvas
static lv_color_t* g_wx_buf = nullptr;
static lv_obj_t* g_rows[4] = {};
static char      g_row_hex[4][8] = {};

// screen positions from last frame (for hit test)
struct Pos { char hex[8]; int16_t x, y; };
static Pos g_pos[MAX_AIRCRAFT];
static int g_pos_n = 0;

static const uint32_t BUF_BYTES = LV_CANVAS_BUF_SIZE_TRUE_COLOR(RADAR_W, RADAR_H);

// ── unit helpers ─────────────────────────────────────────────────────────────
static bool aviation() { return settings_get().units == UNITS_AVIATION; }
static float range_display(int km) { return aviation() ? km / 1.852f : (float)km; }
static const char* range_unit()    { return aviation() ? "NM" : "KM"; }

static const char* compass16(float b) {
    static const char* n[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"};
    return n[((int)lroundf(b / 22.5f)) & 15];
}

// UTF-8 → plain ASCII for the pixel font (ÆØÅ etc. become AE/O/A, other multibyte dropped)
static void ascii_fold(const char* in, char* out, size_t n) {
    size_t o = 0;
    for (const unsigned char* p = (const unsigned char*)in; *p && o + 2 < n; p++) {
        if (*p < 0x80) { out[o++] = toupper(*p); continue; }
        if (*p == 0xC3 && p[1]) {
            unsigned char c = p[1]; p++;
            switch (c) {
                case 0x85: case 0xA5: out[o++] = 'A'; break;                          // Å å
                case 0x86: case 0xA6: out[o++] = 'A'; out[o++] = 'E'; break;          // Æ æ
                case 0x98: case 0xB8: out[o++] = 'O'; break;                          // Ø ø
                case 0x84: case 0xA4: out[o++] = 'A'; break;                          // Ä ä
                case 0x96: case 0xB6: out[o++] = 'O'; break;                          // Ö ö
                case 0x9C: case 0xBC: out[o++] = 'U'; break;                          // Ü ü
                case 0x89: case 0xA9: case 0x88: case 0xA8: out[o++] = 'E'; break;    // É é È è
                default: out[o++] = '?'; break;
            }
            continue;
        }
        while ((p[1] & 0xC0) == 0x80) p++;   // skip continuation bytes of other sequences
    }
    out[o] = 0;
}

// polar (distance km, bearing deg) → canvas x/y
static inline void to_xy(float dist_km, float bearing, int range_km, int16_t& x, int16_t& y) {
    float r = dist_km / (float)range_km * RADAR_R_MAX;
    float a = radians(bearing);
    x = (int16_t)lroundf(RADAR_CX + r * sinf(a));
    y = (int16_t)lroundf(RADAR_CY - r * cosf(a));
}

// ── background (rings, bezel, labels) ────────────────────────────────────────
static void draw_text(lv_coord_t x, lv_coord_t y, lv_coord_t w, const lv_font_t* f, lv_color_t c,
                      lv_text_align_t al, const char* t) {
    lv_draw_label_dsc_t d; lv_draw_label_dsc_init(&d);
    d.font = f; d.color = c; d.align = al;
    lv_canvas_draw_text(g_canvas, x, y, w, &d, t);
}

// map underlay: dotted coastline / lakes / borders, equirectangular around the current centre
static void draw_map() {
    const Settings& s = settings_get();
    const float kx = cosf(radians(s.lat)) * 111.32f / 1000.0f;   // km per milli-degree of longitude
    const float ky = 110.57f / 1000.0f;
    const float scale = (float)RADAR_R_MAX / (float)s.range_km;  // px per km
    const float dlat0 = (s.lat - MAP_LAT0) * 1000.0f, dlon0 = (s.lon - MAP_LON0) * 1000.0f;
    const int r2 = (RADAR_R_MAX - 2) * (RADAR_R_MAX - 2);
    for (size_t si = 0; si < MAP_SEG_COUNT; si++) {
        const MapSeg& sg = MAP_SEGS[si];
        lv_color_t col = sg.kind == MAP_KIND_LAKE ? C_LAKE : sg.kind == MAP_KIND_BORDER ? C_BORDER : C_MAP;
        float step = sg.kind == MAP_KIND_BORDER ? 6.0f : 3.0f;
        float px = 0, py = 0; bool have = false;
        for (uint16_t i = 0; i < sg.count; i++) {
            const int16_t* p = MAP_PTS[sg.start + i];
            float x = RADAR_CX + (p[1] - dlon0) * kx * scale;
            float y = RADAR_CY - (p[0] - dlat0) * ky * scale;
            if (have) {
                float dx = x - px, dy = y - py, len = sqrtf(dx * dx + dy * dy);
                int n = (int)(len / step); if (n < 1) n = 1;
                if (n > 400) n = 400;
                for (int k = 0; k <= n; k++) {
                    int qx = (int)lroundf(px + dx * k / n), qy = (int)lroundf(py + dy * k / n);
                    int ex = qx - RADAR_CX, ey = qy - RADAR_CY;
                    if (ex * ex + ey * ey < r2 && qx >= 0 && qy >= 0 && qx < RADAR_W && qy < RADAR_H)
                        lv_canvas_set_px_color(g_canvas, qx, qy, col);
                }
            }
            px = x; py = y; have = true;
        }
    }
}

static void draw_airports() {
    const Settings& s = settings_get();
    lv_draw_line_dsc_t ln; lv_draw_line_dsc_init(&ln); ln.color = C_TEXT_DIM; ln.width = 1;
    for (size_t i = 0; i < AIRPORT_COUNT; i++) {
        const Airport& ap = AIRPORTS[i];
        float d = geo_distance_km(s.lat, s.lon, ap.lat, ap.lon);
        if (d > s.range_km * 0.98f) continue;
        int16_t x, y; to_xy(d, geo_bearing_deg(s.lat, s.lon, ap.lat, ap.lon), s.range_km, x, y);
        lv_point_t h[2] = {{(lv_coord_t)(x - 4), y}, {(lv_coord_t)(x + 4), y}};
        lv_point_t v[2] = {{x, (lv_coord_t)(y - 4)}, {x, (lv_coord_t)(y + 4)}};
        lv_canvas_draw_line(g_canvas, h, 2, &ln);
        lv_canvas_draw_line(g_canvas, v, 2, &ln);
        draw_text(x + 6, y - 9, 40, &lv_font_unscii_8, C_TEXT_DIM, LV_TEXT_ALIGN_LEFT, ap.iata[0] ? ap.iata : ap.icao);
    }
}

static void draw_background() {
    const Settings& s = settings_get();
    lv_canvas_fill_bg(g_canvas, C_BG, LV_OPA_COVER);
    if (s.map_underlay) draw_map();
    if (s.airports)     draw_airports();

    lv_draw_line_dsc_t ln; lv_draw_line_dsc_init(&ln);
    lv_draw_arc_dsc_t  ar; lv_draw_arc_dsc_init(&ar);

    // bezel: two circles + ticks
    ar.color = C_RING; ar.width = 2;
    lv_canvas_draw_arc(g_canvas, RADAR_CX, RADAR_CY, RADAR_R_TICK, 0, 360, &ar);
    ar.color = C_RING_DIM; ar.width = 1;
    lv_canvas_draw_arc(g_canvas, RADAR_CX, RADAR_CY, RADAR_R_TICK - 9, 0, 360, &ar);
    for (int deg = 0; deg < 360; deg += 5) {
        bool major = (deg % 30) == 0, mid = (deg % 10) == 0;
        float a = radians(deg);
        int len = major ? 9 : (mid ? 6 : 3);
        lv_point_t p[2] = {
            {(lv_coord_t)lroundf(RADAR_CX + (RADAR_R_TICK - len) * sinf(a)), (lv_coord_t)lroundf(RADAR_CY - (RADAR_R_TICK - len) * cosf(a))},
            {(lv_coord_t)lroundf(RADAR_CX + (RADAR_R_TICK - 1)   * sinf(a)), (lv_coord_t)lroundf(RADAR_CY - (RADAR_R_TICK - 1)   * cosf(a))}};
        ln.color = major ? C_TEXT : C_RING; ln.width = major ? 2 : 1;
        lv_canvas_draw_line(g_canvas, p, 2, &ln);
    }
    // degree labels every 30°
    for (int deg = 0; deg < 360; deg += 30) {
        float a = radians(deg);
        char t[6]; snprintf(t, sizeof(t), "%d", deg);
        int x = lroundf(RADAR_CX + RADAR_R_LABEL * sinf(a)) - 12;
        int y = lroundf(RADAR_CY - RADAR_R_LABEL * cosf(a)) - 4;
        draw_text(x, y, 24, &lv_font_unscii_8, C_TEXT_DIM, LV_TEXT_ALIGN_CENTER, t);
    }
    // range rings
    for (int i = 1; i <= 4; i++) {
        int r = RADAR_R_MAX * i / 4;
        ar.color = (i == 4) ? C_RING : C_RING_DIM; ar.width = (i == 4) ? 2 : 1;
        lv_canvas_draw_arc(g_canvas, RADAR_CX, RADAR_CY, r, 0, 360, &ar);
        char t[12];
        float v = range_display(s.range_km) * i / 4.0f;
        if (v < 10) snprintf(t, sizeof(t), "%.1f", v); else snprintf(t, sizeof(t), "%.0f", v);
        draw_text(RADAR_CX + 3, RADAR_CY - r + 1, 40, &lv_font_unscii_8, C_TEXT_DIM, LV_TEXT_ALIGN_LEFT, t);
    }
    // crosshair
    ln.color = C_RING_DIM; ln.width = 1;
    lv_point_t h[2] = {{RADAR_CX - RADAR_R_MAX, RADAR_CY}, {RADAR_CX + RADAR_R_MAX, RADAR_CY}};
    lv_point_t v[2] = {{RADAR_CX, RADAR_CY - RADAR_R_MAX}, {RADAR_CX, RADAR_CY + RADAR_R_MAX}};
    lv_canvas_draw_line(g_canvas, h, 2, &ln);
    lv_canvas_draw_line(g_canvas, v, 2, &ln);
    // centre dot
    lv_draw_rect_dsc_t rc; lv_draw_rect_dsc_init(&rc);
    rc.bg_color = C_TEXT; rc.radius = LV_RADIUS_CIRCLE;
    lv_canvas_draw_rect(g_canvas, RADAR_CX - 2, RADAR_CY - 2, 5, 5, &rc);

    memcpy(g_bg, g_buf, BUF_BYTES);
    g_bg_range = s.range_km; g_bg_units = s.units;
}

// ── aircraft icon: three convex polygons rotated by track ───────────────────
static void rot(float x, float y, float c, float s_, int16_t cx, int16_t cy, lv_point_t& o) {
    o.x = (lv_coord_t)lroundf(cx + x * c - y * s_);
    o.y = (lv_coord_t)lroundf(cy + x * s_ + y * c);
}
static void draw_plane(int16_t x, int16_t y, int track, lv_color_t col, float scale) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = col; d.bg_opa = LV_OPA_COVER; d.border_width = 0;
    if (track < 0) {   // unknown heading → dot
        d.radius = LV_RADIUS_CIRCLE;
        lv_canvas_draw_rect(g_canvas, x - 3, y - 3, 7, 7, &d);
        return;
    }
    float a = radians(track), c = cosf(a), s_ = sinf(a);
    const float fus[][2]  = {{0,-7},{1.6,-4.5},{1.6,4.5},{0,7},{-1.6,4.5},{-1.6,-4.5}};
    const float wing[][2] = {{-7.5,1.5},{7.5,1.5},{2,-2},{-2,-2}};
    const float tail[][2] = {{-3.2,6.5},{3.2,6.5},{1,4},{-1,4}};
    lv_point_t p[6];
    for (int i = 0; i < 6; i++) rot(fus[i][0]*scale,  fus[i][1]*scale,  c, s_, x, y, p[i]);
    lv_canvas_draw_polygon(g_canvas, p, 6, &d);
    for (int i = 0; i < 4; i++) rot(wing[i][0]*scale, wing[i][1]*scale, c, s_, x, y, p[i]);
    lv_canvas_draw_polygon(g_canvas, p, 4, &d);
    for (int i = 0; i < 4; i++) rot(tail[i][0]*scale, tail[i][1]*scale, c, s_, x, y, p[i]);
    lv_canvas_draw_polygon(g_canvas, p, 4, &d);
}

// ── frame ────────────────────────────────────────────────────────────────────
static void draw_frame() {
    const Settings& s = settings_get();
    if (g_bg_range != s.range_km || g_bg_units != s.units) draw_background();
    else memcpy(g_buf, g_bg, BUF_BYTES);

    // sweep wedge (trailing 45°)
    if (s.sweep) {
        lv_draw_rect_dsc_t wd; lv_draw_rect_dsc_init(&wd);
        wd.bg_color = C_SWEEP; wd.bg_opa = 70; wd.border_width = 0;
        lv_point_t p[9]; p[0].x = RADAR_CX; p[0].y = RADAR_CY;
        for (int i = 0; i < 8; i++) {
            float a = radians(g_sweep - 45.0f + i * (45.0f / 7));
            p[i + 1].x = lroundf(RADAR_CX + (RADAR_R_MAX - 1) * sinf(a));
            p[i + 1].y = lroundf(RADAR_CY - (RADAR_R_MAX - 1) * cosf(a));
        }
        lv_canvas_draw_polygon(g_canvas, p, 9, &wd);
        lv_draw_line_dsc_t ld; lv_draw_line_dsc_init(&ld);
        ld.color = C_TEXT; ld.width = 2;
        float a = radians(g_sweep);
        lv_point_t l[2] = {{RADAR_CX, RADAR_CY},
                           {(lv_coord_t)lroundf(RADAR_CX + RADAR_R_MAX * sinf(a)), (lv_coord_t)lroundf(RADAR_CY - RADAR_R_MAX * cosf(a))}};
        lv_canvas_draw_line(g_canvas, l, 2, &ld);
    }

    // header strip
    char t[48];
    snprintf(t, sizeof(t), "LOC %.3f %.3f", s.lat, s.lon);
    draw_text(4, 2, 150, &lv_font_unscii_8, C_TEXT, LV_TEXT_ALIGN_LEFT, t);
    char place[48]; ascii_fold(s.place[0] ? s.place : "no location", place, sizeof(place));
    draw_text(4, 11, 150, &lv_font_unscii_8, C_TEXT_DIM, LV_TEXT_ALIGN_LEFT, place);

    adsb_lock();
    const AdsbStats& st = adsb_stats();
    Aircraft* ac = adsb_list();
    int n = adsb_count();

    snprintf(t, sizeof(t), "RNG %.0f%s", range_display(s.range_km), range_unit());
    draw_text(RADAR_W - 112, 2, 72, &lv_font_unscii_8, C_TEXT, LV_TEXT_ALIGN_RIGHT, t);
    uint32_t age = st.last_ok_ms ? (millis() - st.last_ok_ms) / 1000 : 9999;
    if (age < 9999) snprintf(t, sizeof(t), "%dAC %lus", st.count, (unsigned long)age); else snprintf(t, sizeof(t), "%dAC --", st.count);
    draw_text(RADAR_W - 112, 11, 72, &lv_font_unscii_8, (age > s.update_s * 3) ? C_AC_EMG : C_TEXT_DIM, LV_TEXT_ALIGN_RIGHT, t);

    // trails
    if (s.trails) {
        lv_draw_line_dsc_t td; lv_draw_line_dsc_init(&td);
        td.color = C_RING; td.width = 1; td.opa = 160;
        for (int i = 0; i < n; i++) {
            Aircraft& a = ac[i];
            if (!a.hex[0] || a.trail_n < 1) continue;
            if (!adsb_visible(a)) continue;
            lv_point_t pts[TRAIL_LEN + 1]; int k = 0;
            for (int j = 0; j < a.trail_n; j++) {
                int idx = (a.trail_head - a.trail_n + j + TRAIL_LEN) % TRAIL_LEN;
                float d = geo_distance_km(s.lat, s.lon, a.trail_lat[idx], a.trail_lon[idx]);
                float b = geo_bearing_deg(s.lat, s.lon, a.trail_lat[idx], a.trail_lon[idx]);
                if (d > s.range_km * 1.05f) { k = 0; continue; }
                int16_t x, y; to_xy(d, b, s.range_km, x, y);
                pts[k].x = x; pts[k].y = y; k++;
            }
            if (k == 0) continue;
            int16_t x, y; to_xy(a.dist_km, a.bearing, s.range_km, x, y);
            pts[k].x = x; pts[k].y = y; k++;
            lv_canvas_draw_line(g_canvas, pts, k, &td);
        }
    }

    // great-circle route of the selected aircraft (origin → destination), clipped to the rings
    if (g_sel_hex[0]) {
        int si = adsb_find_hex(g_sel_hex);
        if (si >= 0 && ac[si].org_lat != 0 && ac[si].dst_lat != 0) {
            const Aircraft& a = ac[si];
            lv_draw_line_dsc_t rd; lv_draw_line_dsc_init(&rd); rd.color = C_AC_SEL; rd.width = 1; rd.opa = 150; rd.dash_width = 4; rd.dash_gap = 3;
            float p1 = radians(a.org_lat), l1 = radians(a.org_lon), p2 = radians(a.dst_lat), l2 = radians(a.dst_lon);
            float dd = 2 * asinf(sqrtf(powf(sinf((p2 - p1) / 2), 2) + cosf(p1) * cosf(p2) * powf(sinf((l2 - l1) / 2), 2)));
            lv_point_t pts[66]; int k = 0;
            for (int i = 0; i <= 64; i++) {
                float f = i / 64.0f, A = sinf((1 - f) * dd) / sinf(dd), B = sinf(f * dd) / sinf(dd);
                float x3 = A * cosf(p1) * cosf(l1) + B * cosf(p2) * cosf(l2), y3 = A * cosf(p1) * sinf(l1) + B * cosf(p2) * sinf(l2), z3 = A * sinf(p1) + B * sinf(p2);
                float la = degrees(atan2f(z3, sqrtf(x3 * x3 + y3 * y3))), lo = degrees(atan2f(y3, x3));
                float d = geo_distance_km(s.lat, s.lon, la, lo);
                if (d > s.range_km * 1.05f || dd <= 0) { if (k >= 2) lv_canvas_draw_line(g_canvas, pts, k, &rd); k = 0; continue; }
                int16_t x, y; to_xy(d, geo_bearing_deg(s.lat, s.lon, la, lo), s.range_km, x, y);
                pts[k].x = x; pts[k].y = y; k++;
            }
            if (k >= 2) lv_canvas_draw_line(g_canvas, pts, k, &rd);
        }
    }
    // ISS ground track marker
    if (s.iss) {
        const IssState& iss = extras_iss();
        if (iss.valid && iss.dist_km <= s.range_km * 1.02f) {
            int16_t x, y; to_xy(iss.dist_km, iss.bearing, s.range_km, x, y);
            lv_draw_arc_dsc_t ad; lv_draw_arc_dsc_init(&ad); ad.color = C_AC_SEL; ad.width = 2;
            lv_canvas_draw_arc(g_canvas, x, y, 7, 0, 360, &ad);
            lv_draw_line_dsc_t il; lv_draw_line_dsc_init(&il); il.color = C_AC_SEL; il.width = 2;
            lv_point_t s1[2] = {{(lv_coord_t)(x - 12), y}, {(lv_coord_t)(x - 8), y}}, s2[2] = {{(lv_coord_t)(x + 8), y}, {(lv_coord_t)(x + 12), y}};
            lv_canvas_draw_line(g_canvas, s1, 2, &il); lv_canvas_draw_line(g_canvas, s2, 2, &il);
            draw_text(x + 14, y - 4, 40, &lv_font_unscii_8, C_AC_SEL, LV_TEXT_ALIGN_LEFT, "ISS");
        }
    }

    // aircraft (labels are skipped when they would overlap an already-placed label)
    g_pos_n = 0;
    int16_t lbl_x[MAX_AIRCRAFT], lbl_y[MAX_AIRCRAFT]; int lbl_n = 0;
    for (int i = 0; i < n; i++) {
        Aircraft& a = ac[i];
        if (!a.hex[0]) continue;
        if (!adsb_visible(a)) continue;
        if (a.dist_km > s.range_km * 1.04f) continue;
        int16_t x, y; to_xy(a.dist_km, a.bearing, s.range_km, x, y);
        bool sel = g_sel_hex[0] && !strcmp(g_sel_hex, a.hex);
        lv_color_t col = a.emergency ? C_AC_EMG : (a.watch || a.notable) ? C_AC_SEL : (a.military && s.highlight_mil) ? C_AC_MIL
                       : a.on_ground ? C_AC_GND : a.cls == CLS_LIGHT ? C_AC_LIGHT : a.cls == CLS_HELI ? C_AC_HELI : C_AC;
        if (sel) col = C_AC_SEL;
        draw_plane(x, y, a.track, col, sel ? 1.25f : 1.0f);
        if (sel || a.emergency || a.alert) {
            lv_draw_arc_dsc_t ad; lv_draw_arc_dsc_init(&ad);
            ad.color = a.alert ? C_AC_SEL : col; ad.width = a.alert ? 2 : 1;
            lv_canvas_draw_arc(g_canvas, x, y, a.alert ? (g_blink ? 16 : 11) : 12, 0, 360, &ad);
        }
        if (s.labels) {
            bool clash = false;
            for (int k = 0; k < lbl_n && !clash; k++)
                if (abs(lbl_x[k] - x) < 56 && abs(lbl_y[k] - y) < 10) clash = true;
            if (!clash || sel) {
                const char* name = a.flight[0] ? a.flight : (a.reg[0] ? a.reg : a.hex);
                if (x > RADAR_W - 72) draw_text(x - 79, y - 4, 70, &lv_font_unscii_8, sel ? C_AC_SEL : C_TEXT_DIM, LV_TEXT_ALIGN_RIGHT, name);
                else                  draw_text(x + 9,  y - 4, 70, &lv_font_unscii_8, sel ? C_AC_SEL : C_TEXT_DIM, LV_TEXT_ALIGN_LEFT,  name);
                if (lbl_n < MAX_AIRCRAFT) { lbl_x[lbl_n] = x; lbl_y[lbl_n] = y; lbl_n++; }
            }
        }
        if (g_pos_n < MAX_AIRCRAFT) { strlcpy(g_pos[g_pos_n].hex, a.hex, 8); g_pos[g_pos_n].x = x; g_pos[g_pos_n].y = y; g_pos_n++; }
    }
    // overhead banner (nearest alert)
    if (st.alerts > 0) {
        int best = -1;
        for (int i = 0; i < n; i++) if (ac[i].hex[0] && ac[i].alert && (best < 0 || ac[i].cpa_min < ac[best].cpa_min)) best = i;
        if (best >= 0 && g_blink) {
            char b[48];
            const Aircraft& a = ac[best];
            if (a.cpa_min < 0.5f) snprintf(b, sizeof(b), "OVERHEAD %s LOOK %s %.0fUP", a.flight[0] ? a.flight : a.hex, adsb_compass16(a.bearing), a.elev_deg);
            else snprintf(b, sizeof(b), "%s %.1fKM IN %.0fMIN LOOK %s", a.flight[0] ? a.flight : a.hex, a.cpa_km, a.cpa_min, adsb_compass16(a.bearing));
            draw_text(RADAR_CX - 100, RADAR_CY + 70, 200, &lv_font_unscii_8, C_AC_SEL, LV_TEXT_ALIGN_CENTER, b);
        }
    }
    adsb_unlock();
    lv_obj_invalidate(g_canvas);
}

// ── bottom panel: 4 closest + weather ────────────────────────────────────────
static void fmt_row(const Aircraft& a, char* l1, size_t n1, char* l2, size_t n2, char* l3, size_t n3) {
    snprintf(l1, n1, "%s   %s   %s", a.flight[0] ? a.flight : a.hex, a.type[0] ? a.type : "----", a.reg);
    const char* airline = adsb_airline_name(a.flight);
    if (a.route[0]) {
        char org[4] = {0}, dst[4] = {0};
        strncpy(org, a.route, 3);
        size_t rl = strlen(a.route); if (rl >= 3) strncpy(dst, a.route + rl - 3, 3);
        snprintf(l2, n2, "%s " LV_SYMBOL_RIGHT " %s %s   %s", org, dst, a.dest_city, airline);
    } else {
        snprintf(l2, n2, "%s   %s", a.route_tried ? "no route" : "...", airline[0] ? airline : a.military ? "military" : (a.flight[0] ? "private / GA" : ""));
    }
    const char* climb = (a.vr_fpm > 300) ? LV_SYMBOL_UP : (a.vr_fpm < -300) ? LV_SYMBOL_DOWN : "";
    if (a.on_ground) {
        snprintf(l3, n3, "%.1f %s   %03.0f\xc2\xb0   on ground", aviation() ? a.dist_km / 1.852f : a.dist_km, aviation() ? "nm" : "km", a.bearing);
        return;
    }
    if (aviation())
        snprintf(l3, n3, "%.1f nm   %03.0f\xc2\xb0   FL%03d %s   %d kt", a.dist_km / 1.852f, a.bearing, (int)(a.alt_ft / 100), climb, a.gs_kt);
    else
        snprintf(l3, n3, "%.1f km   %03.0f\xc2\xb0   %d m %s   %d km/h", a.dist_km, a.bearing, (int)lroundf(a.alt_ft * 0.3048f), climb, (int)lroundf(a.gs_kt * 1.852f));
}

static void refresh_bottom() {
    const Settings& s = settings_get();
    adsb_lock();
    Aircraft* ac = adsb_list(); int n = adsb_count();
    // pick 4 closest (filtered)
    int best[4] = {-1, -1, -1, -1};
    for (int i = 0; i < n; i++) {
        if (!ac[i].hex[0]) continue;
        if (!adsb_visible(ac[i])) continue;
        for (int k = 0; k < 4; k++) {
            if (best[k] < 0 || ac[i].dist_km < ac[best[k]].dist_km) {
                for (int m = 3; m > k; m--) best[m] = best[m - 1];
                best[k] = i; break;
            }
        }
    }
    for (int k = 0; k < 4; k++) {
        lv_obj_t* row = g_rows[k];
        lv_obj_t* l1 = lv_obj_get_child(row, 0);
        lv_obj_t* l2 = lv_obj_get_child(row, 1);
        lv_obj_t* l3 = lv_obj_get_child(row, 2);
        if (best[k] < 0) {
            g_row_hex[k][0] = 0;
            lv_label_set_text(l1, k == 0 ? (net_wifi_connected() ? "No aircraft in range" : "WiFi not connected") : "");
            lv_label_set_text(l2, ""); lv_label_set_text(l3, "");
            lv_obj_set_style_border_width(row, 0, 0);
            continue;
        }
        const Aircraft& a = ac[best[k]];
        char l1t[48], l2t[64], l3t[64];
        fmt_row(a, l1t, sizeof(l1t), l2t, sizeof(l2t), l3t, sizeof(l3t));
        if (a.emergency) snprintf(l3t, sizeof(l3t), "EMERGENCY  squawk %s  %.1f km", a.squawk, a.dist_km);
        lv_label_set_text(l1, l1t);
        lv_label_set_text(l2, l2t);
        lv_label_set_text(l3, l3t);
        strlcpy(g_row_hex[k], a.hex, 8);
        lv_color_t c = a.emergency ? C_AC_EMG : (a.alert || a.watch || a.notable) ? C_AC_SEL : (a.military && s.highlight_mil) ? C_AC_MIL : C_TEXT;
        bool sel = g_sel_hex[0] && !strcmp(g_sel_hex, a.hex);
        lv_obj_set_style_text_color(l1, sel ? C_AC_SEL : c, 0);
        lv_obj_set_style_border_width(row, (sel || a.alert) ? 1 : 0, 0);
        lv_obj_set_style_bg_color(row, (a.alert && g_blink) ? lv_color_hex(0x1C4A14) : ((k % 2) ? lv_color_hex(0x04120A) : C_BG), 0);
    }
    adsb_unlock();
}

// ── tiny procedural weather icon (22×22, green monochrome) ───────────────────
static void draw_wx_icon(int code) {
    if (!g_wx_icon) return;
    lv_canvas_fill_bg(g_wx_icon, C_BG, LV_OPA_COVER);
    lv_draw_rect_dsc_t r; lv_draw_rect_dsc_init(&r); r.bg_color = C_TEXT; r.border_width = 0; r.radius = LV_RADIUS_CIRCLE;
    lv_draw_line_dsc_t l; lv_draw_line_dsc_init(&l); l.color = C_TEXT; l.width = 2; l.round_start = l.round_end = true;
    lv_draw_arc_dsc_t a; lv_draw_arc_dsc_init(&a); a.color = C_TEXT; a.width = 2;
    bool sun   = (code == 0 || code == 1);
    bool cloud = (code >= 2 && code <= 3) || code >= 51;
    bool part  = (code == 1 || code == 2);
    bool rain  = (code >= 51 && code <= 67) || (code >= 80 && code <= 82);
    bool snow  = (code >= 71 && code <= 77) || code == 85 || code == 86;
    bool fog   = (code == 45 || code == 48);
    bool storm = code >= 95;
    if (sun || part) {
        int cx = part ? 8 : 11, cy = part ? 8 : 11, rr = part ? 3 : 4;
        lv_canvas_draw_rect(g_wx_icon, cx - rr, cy - rr, 2 * rr + 1, 2 * rr + 1, &r);
        for (int k = 0; k < 8; k++) {
            float an = k * 0.785398f;
            lv_point_t p[2] = {{(lv_coord_t)(cx + cosf(an) * (rr + 2)), (lv_coord_t)(cy + sinf(an) * (rr + 2))},
                               {(lv_coord_t)(cx + cosf(an) * (rr + 5)), (lv_coord_t)(cy + sinf(an) * (rr + 5))}};
            l.width = 1; lv_canvas_draw_line(g_wx_icon, p, 2, &l);
        }
    }
    if (cloud || fog || storm) {
        int y = (rain || snow || storm) ? 8 : 11;
        lv_draw_rect_dsc_t c; lv_draw_rect_dsc_init(&c); c.bg_color = C_TEXT; c.border_width = 0; c.radius = LV_RADIUS_CIRCLE;
        lv_canvas_draw_rect(g_wx_icon, 4,  y - 3, 9, 9, &c);
        lv_canvas_draw_rect(g_wx_icon, 10, y - 6, 11, 11, &c);
        c.radius = 2; lv_canvas_draw_rect(g_wx_icon, 4, y + 1, 17, 5, &c);
        if (fog) {
            l.width = 1;
            for (int k = 0; k < 3; k++) { lv_point_t p[2] = {{3, (lv_coord_t)(14 + k * 3)}, {20, (lv_coord_t)(14 + k * 3)}}; lv_canvas_draw_line(g_wx_icon, p, 2, &l); }
        }
    }
    if (rain) { l.width = 1; for (int k = 0; k < 3; k++) { lv_point_t p[2] = {{(lv_coord_t)(7 + k * 5), 16}, {(lv_coord_t)(5 + k * 5), 21}}; lv_canvas_draw_line(g_wx_icon, p, 2, &l); } }
    if (snow) { r.radius = LV_RADIUS_CIRCLE; for (int k = 0; k < 3; k++) lv_canvas_draw_rect(g_wx_icon, 5 + k * 5, 17 + (k & 1) * 2, 3, 3, &r); }
    if (storm) {
        lv_draw_rect_dsc_t b; lv_draw_rect_dsc_init(&b); b.bg_color = C_AC_SEL; b.border_width = 0;
        lv_point_t p[5] = {{12, 12}, {8, 18}, {11, 18}, {9, 22}, {15, 15}};
        lv_canvas_draw_polygon(g_wx_icon, p, 5, &b);
    }
    lv_obj_invalidate(g_wx_icon);
}

void ui_radar_notify_weather() {
    const LocalInfo& li = net_local_info();
    char t[48], w[48];
    if (li.valid) {
        snprintf(t, sizeof(t), "%.0f\xc2\xb0""C  %s", li.temp_c, geo_wmo_short(li.wmo_code));
        if (aviation())
            snprintf(w, sizeof(w), "Wind %.0f kt %s  G %.0f", li.wind_mps * 1.9438f, compass16(li.wind_dir), li.gust_mps * 1.9438f);
        else
            snprintf(w, sizeof(w), "Wind %.1f m/s %s  G %.1f", li.wind_mps, compass16(li.wind_dir), li.gust_mps);
        draw_wx_icon(li.wmo_code);
        lv_obj_clear_flag(g_wx_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        char st[96]; net_status(st, sizeof(st));
        strlcpy(t, st, sizeof(t)); w[0] = 0;
        lv_obj_add_flag(g_wx_icon, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(g_lbl_wx, t);
    lv_label_set_text(g_lbl_wind, w);
}

// ── events ───────────────────────────────────────────────────────────────────
static void canvas_click_cb(lv_event_t* e) {
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p; lv_indev_get_point(indev, &p);
    lv_area_t a; lv_obj_get_coords(g_canvas, &a);
    int x = p.x - a.x1, y = p.y - a.y1;
    int best = -1; long bd = 22 * 22;
    for (int i = 0; i < g_pos_n; i++) {
        long dx = g_pos[i].x - x, dy = g_pos[i].y - y, d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = i; }
    }
    if (best >= 0) strlcpy(g_sel_hex, g_pos[best].hex, 8); else g_sel_hex[0] = 0;
    g_dirty = true; refresh_bottom();
}
static void row_click_cb(lv_event_t* e) {
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    if (!g_row_hex[k][0]) return;
    strlcpy(g_sel_hex, g_row_hex[k], 8);
    g_dirty = true; refresh_bottom();
    ui_detail_show(g_row_hex[k]);
    ui_main_goto_tab(PAGE_DETAIL);
}
static void zoom_cb(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    settings_set_range_index(settings_range_index() + dir);
    Serial.printf("[UI] zoom %+d -> %d km\n", dir, settings_get().range_km);
    settings_save();
    ui_radar_range_changed();
    ui_main_request_fetch();
}
static void nav_cb(lv_event_t* e) { ui_main_goto_tab((int)(intptr_t)lv_event_get_user_data(e)); }

static void timer_cb(lv_timer_t*) {
    if (!g_active) return;
    const Settings& s = settings_get();
    if (++g_blink_ticks >= 6) {   // ~0.5 s blink
        g_blink_ticks = 0; g_blink = !g_blink;
        adsb_lock(); bool any = adsb_stats().alerts > 0; adsb_unlock();
        if (any) { g_dirty = true; refresh_bottom(); }
    }
    if (s.sweep) { g_sweep += 3.0f; if (g_sweep >= 360) g_sweep -= 360; draw_frame(); g_dirty = false; }
    else if (g_dirty) { draw_frame(); g_dirty = false; }
}

static lv_obj_t* corner_btn(lv_obj_t* parent, int x, int y, const char* txt, const lv_font_t* f, lv_event_cb_t cb, int ud) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_size(b, 30, 24);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x0B2E16), 0);
    lv_obj_set_style_bg_opa(b, 200, 0);
    lv_obj_set_style_border_color(b, C_RING, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, C_TEXT, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)(intptr_t)ud);
    return b;
}

void ui_radar_build(lv_obj_t* parent) {
    lv_obj_set_style_bg_color(parent, C_BG, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_scroll_dir(parent, LV_DIR_NONE);

    g_buf = (lv_color_t*)heap_caps_malloc(BUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_bg  = (lv_color_t*)heap_caps_malloc(BUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    g_canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(g_canvas, g_buf, RADAR_W, RADAR_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(g_canvas, 0, 0);
    lv_obj_add_flag(g_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_canvas, canvas_click_cb, LV_EVENT_CLICKED, nullptr);

    corner_btn(parent, RADAR_W - 34, 20, "+", &lv_font_montserrat_16, zoom_cb, +1);
    corner_btn(parent, RADAR_W - 34, 46, "-", &lv_font_montserrat_16, zoom_cb, -1);
    corner_btn(parent, 4,            RADAR_H - 28, LV_SYMBOL_LIST,     &lv_font_montserrat_14, nav_cb, PAGE_LIST);
    corner_btn(parent, RADAR_W - 34, RADAR_H - 28, LV_SYMBOL_SETTINGS, &lv_font_montserrat_14, nav_cb, PAGE_SETTINGS);

    // bottom panel
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, RADAR_W, BOTTOM_H);
    lv_obj_set_pos(panel, 0, BOTTOM_Y);
    lv_obj_set_style_bg_color(panel, C_BG, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, C_RING_DIM, 0);
    lv_obj_set_style_border_side(panel, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_scroll_dir(panel, LV_DIR_NONE);

    g_wx_buf = (lv_color_t*)heap_caps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(22, 22), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_wx_icon = lv_canvas_create(panel);
    lv_canvas_set_buffer(g_wx_icon, g_wx_buf, 22, 22, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(g_wx_icon, 4, 2);
    lv_obj_add_flag(g_wx_icon, LV_OBJ_FLAG_HIDDEN);

    g_lbl_wx = lv_label_create(panel);
    lv_obj_set_pos(g_lbl_wx, 30, 5);
    lv_obj_set_width(g_lbl_wx, 72);
    lv_label_set_long_mode(g_lbl_wx, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(g_lbl_wx, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_lbl_wx, C_TEXT, 0);
    lv_label_set_text(g_lbl_wx, "Starting...");

    g_lbl_wind = lv_label_create(panel);
    lv_obj_set_pos(g_lbl_wind, 102, 5);
    lv_obj_set_width(g_lbl_wind, RADAR_W - 102 - 6);
    lv_label_set_long_mode(g_lbl_wind, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(g_lbl_wind, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(g_lbl_wind, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_lbl_wind, C_TEXT, 0);
    lv_label_set_text(g_lbl_wind, "");

    const int row_h = 40, row_y0 = 26;
    for (int k = 0; k < 4; k++) {
        lv_obj_t* row = lv_obj_create(panel);
        g_rows[k] = row;
        lv_obj_set_size(row, RADAR_W - 4, row_h);
        lv_obj_set_pos(row, 2, row_y0 + k * (row_h + 1));
        lv_obj_set_style_bg_color(row, (k % 2) ? lv_color_hex(0x04120A) : C_BG, 0);
        lv_obj_set_style_border_color(row, C_AC_SEL, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 2, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_left(row, 4, 0);
        lv_obj_set_scroll_dir(row, LV_DIR_NONE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)k);

        const lv_font_t* fonts[3] = {&lv_font_montserrat_12, &lv_font_montserrat_10, &lv_font_montserrat_10};
        const int ys[3] = {1, 15, 27};
        for (int li = 0; li < 3; li++) {
            lv_obj_t* l = lv_label_create(row);
            lv_obj_set_pos(l, 0, ys[li]);
            lv_obj_set_width(l, RADAR_W - 12);
            lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_font(l, fonts[li], 0);
            lv_obj_set_style_text_color(l, li == 0 ? C_TEXT : li == 1 ? lv_color_hex(0x33D650) : C_TEXT_DIM, 0);
            lv_label_set_text(l, "");
        }
    }

    draw_background();
    draw_frame();
    refresh_bottom();
    g_timer = lv_timer_create(timer_cb, 80, nullptr);
}

void ui_radar_notify_data()   { g_dirty = true; refresh_bottom(); }
void ui_radar_range_changed() { g_bg_range = -1; g_dirty = true; refresh_bottom(); ui_radar_notify_weather(); }
void ui_radar_select(const char* hex) {
    if (hex) strlcpy(g_sel_hex, hex, 8); else g_sel_hex[0] = 0;
    g_dirty = true; refresh_bottom();
}
void ui_radar_set_active(bool active) { g_active = active; if (active) g_dirty = true; }
