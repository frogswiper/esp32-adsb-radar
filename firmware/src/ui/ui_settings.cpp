#include "ui_settings.h"
#include "ui_main.h"
#include "ui_radar.h"
#include "../board_config.h"
#include "../settings.h"
#include "../countries.h"
#include "../net_task.h"
#include "../display.h"
#include <Arduino.h>

LV_FONT_DECLARE(font_montserrat_14_nor);

#define C_HDR  lv_color_hex(0x3DF25A)
#define C_TXT  lv_color_hex(0x33D650)
#define C_DIM  lv_color_hex(0x1E9A45)
#define C_BTN  lv_color_hex(0x07200F)
#define C_LINE lv_color_hex(0x1FA84A)

// ── keyboard maps (Norwegian letters included) ───────────────────────────────
static const char* kb_map_lc[] = {
    "q","w","e","r","t","y","u","i","o","p","\n",
    "a","s","d","f","g","h","j","k","l","\n",
    "ABC","z","x","c","v","b","n","m",LV_SYMBOL_BACKSPACE,"\n",
    "1#","\xc3\xa6","\xc3\xb8","\xc3\xa5",","," ",LV_SYMBOL_OK,""
};
static const lv_btnmatrix_ctrl_t kb_ctrl_lc[] = {
    1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,
    LV_KEYBOARD_CTRL_BTN_FLAGS|2, 1,1,1,1,1,1,1, LV_KEYBOARD_CTRL_BTN_FLAGS|2,
    LV_KEYBOARD_CTRL_BTN_FLAGS|1, 1,1,1,1, 4, LV_KEYBOARD_CTRL_BTN_FLAGS|2
};
static const char* kb_map_uc[] = {
    "Q","W","E","R","T","Y","U","I","O","P","\n",
    "A","S","D","F","G","H","J","K","L","\n",
    "abc","Z","X","C","V","B","N","M",LV_SYMBOL_BACKSPACE,"\n",
    "1#","\xc3\x86","\xc3\x98","\xc3\x85",","," ",LV_SYMBOL_OK,""
};
static const lv_btnmatrix_ctrl_t kb_ctrl_uc[] = {
    1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,
    LV_KEYBOARD_CTRL_BTN_FLAGS|2, 1,1,1,1,1,1,1, LV_KEYBOARD_CTRL_BTN_FLAGS|2,
    LV_KEYBOARD_CTRL_BTN_FLAGS|1, 1,1,1,1, 4, LV_KEYBOARD_CTRL_BTN_FLAGS|2
};

static lv_obj_t *g_form, *g_kb;
static lv_obj_t *g_ta_ssid, *g_ta_pass, *g_ta_city, *g_ta_lat, *g_ta_lon;
static lv_obj_t *g_dd_mode, *g_dd_cont, *g_dd_country, *g_dd_city;
static lv_obj_t *g_dd_range, *g_dd_update, *g_dd_units, *g_dd_source;
static lv_obj_t *g_sw_sweep, *g_sw_trails, *g_sw_labels, *g_sw_ground, *g_sw_auto, *g_sw_night, *g_sw_mil;
static lv_obj_t *g_dd_alert, *g_sw_alertbr, *g_sw_map, *g_sw_apt;
static lv_obj_t *g_sw_cls[5], *g_sw_iss, *g_sw_metar, *g_ta_watch, *g_ta_ntfy;
static const uint8_t ALERT_STEPS[] = {0, 1, 2, 3, 5, 10};
static lv_obj_t *g_sl_bright, *g_lbl_status, *g_lbl_loc, *g_box_city, *g_box_manual, *g_box_auto;
static const int KB_H = 200;

static const uint16_t UPDATE_STEPS[] = {2, 3, 5, 10, 15, 30, 60};
static const int UPDATE_COUNT = 7;

// country index map for the current continent dropdown
static int g_country_map[COUNTRY_COUNT];
static int g_country_n = 0;

// green monochrome look for inputs
static void green_input(lv_obj_t* o) {
    lv_obj_set_style_bg_color(o, lv_color_hex(0x04120A), 0);
    lv_obj_set_style_border_color(o, C_LINE, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_text_color(o, C_TXT, 0);
    lv_obj_set_style_text_color(o, C_DIM, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(o, C_HDR, LV_STATE_FOCUSED);
}
static void status(const char* m) { if (g_lbl_status) lv_label_set_text(g_lbl_status, m); }

// ── keyboard show/hide ───────────────────────────────────────────────────────
static void kb_show(lv_obj_t* ta, bool numeric) {
    lv_keyboard_set_textarea(g_kb, ta);
    lv_keyboard_set_mode(g_kb, numeric ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_clear_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(g_form, CONTENT_HEIGHT - KB_H);
    lv_obj_scroll_to_view(ta, LV_ANIM_OFF);
}
static void kb_hide() {
    lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(g_form, CONTENT_HEIGHT);
    lv_keyboard_set_textarea(g_kb, nullptr);
}
static void ta_cb(lv_event_t* e) {
    lv_event_code_t c = lv_event_get_code(e);
    lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
    if (c == LV_EVENT_FOCUSED || c == LV_EVENT_CLICKED) kb_show(ta, ta == g_ta_lat || ta == g_ta_lon);
}
static void kb_cb(lv_event_t* e) {
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_READY || c == LV_EVENT_CANCEL) kb_hide();
}

// ── location helpers ─────────────────────────────────────────────────────────
static void fill_countries(int continent) {
    static char opts[COUNTRY_COUNT * 28];
    size_t o = 0; g_country_n = 0;
    for (size_t i = 0; i < COUNTRY_COUNT; i++) {
        if (COUNTRIES[i].continent != continent) continue;
        g_country_map[g_country_n++] = i;
        int w = snprintf(opts + o, sizeof(opts) - o, "%s%s", o ? "\n" : "", COUNTRIES[i].name);
        if (w < 0 || o + w >= sizeof(opts)) break;
        o += w;
    }
    lv_dropdown_set_options(g_dd_country, g_country_n ? opts : "-");
}
static const CountryEntry* current_country() {
    int sel = lv_dropdown_get_selected(g_dd_country);
    if (sel < 0 || sel >= g_country_n) return nullptr;
    return &COUNTRIES[g_country_map[sel]];
}
static void refresh_loc_label() {
    const Settings& s = settings_get();
    static const char* modes[] = {"IP", "city", "manual"};
    char t[96];
    snprintf(t, sizeof(t), "Current: %s  (%.3f, %.3f)  [%s]", s.place[0] ? s.place : "none", s.lat, s.lon, modes[s.loc_mode > 2 ? 0 : s.loc_mode]);
    lv_label_set_text(g_lbl_loc, t);
}
static void show_mode_boxes() {
    int m = lv_dropdown_get_selected(g_dd_mode);
    lv_obj_add_flag(g_box_auto,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_box_city,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_box_manual, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(m == 0 ? g_box_auto : m == 1 ? g_box_city : g_box_manual, LV_OBJ_FLAG_HIDDEN);
}

// ── callbacks ────────────────────────────────────────────────────────────────
static void save_wifi_cb(lv_event_t*) {
    Settings& s = settings_get();
    strlcpy(s.wifi_ssid,     lv_textarea_get_text(g_ta_ssid), sizeof(s.wifi_ssid));
    strlcpy(s.wifi_password, lv_textarea_get_text(g_ta_pass), sizeof(s.wifi_password));
    settings_save();
    kb_hide();
    status("Connecting...");
    net_send(NC_CONNECT_WIFI);
}
static void mode_cb(lv_event_t*) { show_mode_boxes(); }
static void cont_cb(lv_event_t*) {
    int c = lv_dropdown_get_selected(g_dd_cont);
    settings_get().continent = c;
    fill_countries(c);
    lv_dropdown_set_options(g_dd_city, "- load -");
}
static void country_cb(lv_event_t*) {
    const CountryEntry* ce = current_country();
    if (!ce) return;
    lv_dropdown_set_options(g_dd_city, "loading...");
    net_send(NC_FETCH_CITIES, ce->name);
}
static void load_cities_cb(lv_event_t*) { country_cb(nullptr); }
static void use_city_cb(lv_event_t*) {
    const CountryEntry* ce = current_country();
    char city[48];
    const char* typed = lv_textarea_get_text(g_ta_city);
    if (typed && typed[0]) strlcpy(city, typed, sizeof(city));
    else lv_dropdown_get_selected_str(g_dd_city, city, sizeof(city));
    if (!city[0] || !strcmp(city, "- load -") || !strcmp(city, "loading...") || !strcmp(city, "-")) { status("Pick or type a city first"); return; }
    kb_hide();
    status("Looking up city...");
    net_send(NC_GEOCODE, city, ce ? ce->cc : "");
}
static void use_manual_cb(lv_event_t*) {
    float lat = atof(lv_textarea_get_text(g_ta_lat));
    float lon = atof(lv_textarea_get_text(g_ta_lon));
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180 || (lat == 0 && lon == 0)) { status("Invalid coordinates"); return; }
    kb_hide();
    net_send(NC_APPLY_MANUAL, nullptr, nullptr, lat, lon);
}
static void locate_cb(lv_event_t*) { status("Locating..."); net_send(NC_LOCATE_IP); }
static void save_integrations_cb(lv_event_t*) {
    Settings& s = settings_get();
    strlcpy(s.watchlist,  lv_textarea_get_text(g_ta_watch), sizeof(s.watchlist));
    strlcpy(s.ntfy_topic, lv_textarea_get_text(g_ta_ntfy),  sizeof(s.ntfy_topic));
    settings_save(); kb_hide(); status("Integrations saved");
}

static void radar_opts_cb(lv_event_t* e) {
    Settings& s = settings_get();
    lv_obj_t* t = (lv_obj_t*)lv_event_get_target(e);
    bool fetch = false;
    if (t == g_dd_range)  { settings_set_range_index(lv_dropdown_get_selected(g_dd_range)); fetch = true; }
    if (t == g_dd_update) { s.update_s = UPDATE_STEPS[lv_dropdown_get_selected(g_dd_update)]; }
    if (t == g_dd_units)  { s.units = lv_dropdown_get_selected(g_dd_units); }
    if (t == g_dd_source) { s.source = lv_dropdown_get_selected(g_dd_source); fetch = true; }
    if (t == g_dd_alert)  { s.alert_km = ALERT_STEPS[lv_dropdown_get_selected(g_dd_alert)]; }
    s.alert_bright = lv_obj_has_state(g_sw_alertbr, LV_STATE_CHECKED);
    s.map_underlay = lv_obj_has_state(g_sw_map, LV_STATE_CHECKED);
    s.airports     = lv_obj_has_state(g_sw_apt, LV_STATE_CHECKED);
    s.iss          = lv_obj_has_state(g_sw_iss, LV_STATE_CHECKED);
    s.metar        = lv_obj_has_state(g_sw_metar, LV_STATE_CHECKED);
    uint8_t mask = 0; for (int i = 0; i < 5; i++) if (lv_obj_has_state(g_sw_cls[i], LV_STATE_CHECKED)) mask |= 1 << i;
    s.class_mask = mask ? mask : 0x1F;
    s.sweep       = lv_obj_has_state(g_sw_sweep,  LV_STATE_CHECKED);
    s.trails      = lv_obj_has_state(g_sw_trails, LV_STATE_CHECKED);
    s.labels      = lv_obj_has_state(g_sw_labels, LV_STATE_CHECKED);
    s.hide_ground = lv_obj_has_state(g_sw_ground, LV_STATE_CHECKED);
    s.auto_range  = lv_obj_has_state(g_sw_auto,   LV_STATE_CHECKED);
    s.night_dim   = lv_obj_has_state(g_sw_night,  LV_STATE_CHECKED);
    s.highlight_mil = lv_obj_has_state(g_sw_mil,  LV_STATE_CHECKED);
    s.brightness  = lv_slider_get_value(g_sl_bright);
    settings_save();
    display_set_brightness(s.brightness);
    ui_radar_range_changed();
    if (fetch) ui_main_request_fetch();
}

// ── widget factories ─────────────────────────────────────────────────────────
static lv_obj_t* header(lv_obj_t* p, const char* t) {
    lv_obj_t* h = lv_label_create(p);
    lv_label_set_text(h, t);
    lv_obj_set_style_text_font(h, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(h, C_HDR, 0);
    lv_obj_set_style_pad_top(h, 6, 0);
    return h;
}
static lv_obj_t* textarea(lv_obj_t* p, const char* ph, const char* val, bool pw) {
    lv_obj_t* ta = lv_textarea_create(p);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, ph);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_style_text_font(ta, &font_montserrat_14_nor, 0);
    if (pw) lv_textarea_set_password_mode(ta, true);
    if (val && val[0]) lv_textarea_set_text(ta, val);
    green_input(ta);
    lv_obj_set_style_bg_color(ta, C_HDR, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_add_event_cb(ta, ta_cb, LV_EVENT_ALL, nullptr);
    return ta;
}
static lv_obj_t* button(lv_obj_t* p, const char* t, lv_event_cb_t cb) {
    lv_obj_t* b = lv_btn_create(p);
    lv_obj_set_size(b, LV_PCT(100), 38);
    lv_obj_set_style_bg_color(b, C_BTN, 0);
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
static lv_obj_t* labeled_row(lv_obj_t* p, const char* t) {
    lv_obj_t* r = lv_obj_create(p);
    lv_obj_set_size(r, LV_PCT(100), 40);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* l = lv_label_create(r);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, C_TXT, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    return r;
}
static lv_obj_t* dropdown(lv_obj_t* p, const char* label, const char* opts, int sel, lv_event_cb_t cb, int w = 150) {
    lv_obj_t* r = labeled_row(p, label);
    lv_obj_t* d = lv_dropdown_create(r);
    lv_obj_set_width(d, w);
    lv_dropdown_set_options(d, opts);
    lv_dropdown_set_selected(d, sel);
    lv_obj_set_style_text_font(d, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(d), &lv_font_montserrat_14, 0);
    lv_obj_set_style_max_height(lv_dropdown_get_list(d), 260, 0);
    green_input(d);
    lv_obj_t* list = lv_dropdown_get_list(d);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x04120A), 0);
    lv_obj_set_style_border_color(list, C_LINE, 0);
    lv_obj_set_style_text_color(list, C_TXT, 0);
    lv_obj_set_style_bg_color(list, C_LINE, LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(list, lv_color_black(), LV_PART_SELECTED | LV_STATE_CHECKED);
    if (cb) lv_obj_add_event_cb(d, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return d;
}
static lv_obj_t* toggle(lv_obj_t* p, const char* label, bool on) {
    lv_obj_t* r = labeled_row(p, label);
    lv_obj_t* sw = lv_switch_create(r);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x0B2E16), 0);
    lv_obj_set_style_bg_color(sw, C_LINE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, C_TXT, LV_PART_KNOB);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, radar_opts_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return sw;
}
static lv_obj_t* box(lv_obj_t* p) {
    lv_obj_t* b = lv_obj_create(p);
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_pad_row(b, 6, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    return b;
}

// ── build ────────────────────────────────────────────────────────────────────
void ui_settings_build(lv_obj_t* parent) {
    Settings& s = settings_get();

    g_form = lv_obj_create(parent);
    lv_obj_set_size(g_form, DISPLAY_WIDTH, CONTENT_HEIGHT);
    lv_obj_set_pos(g_form, 0, 0);
    lv_obj_set_style_bg_color(g_form, lv_color_black(), 0);
    lv_obj_set_style_border_width(g_form, 0, 0);
    lv_obj_set_style_radius(g_form, 0, 0);
    lv_obj_set_style_pad_all(g_form, 10, 0);
    lv_obj_set_style_pad_row(g_form, 6, 0);
    lv_obj_set_flex_flow(g_form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_form, LV_DIR_VER);
    lv_obj_set_style_bg_color(g_form, lv_color_hex(0x1FA84A), LV_PART_SCROLLBAR);

    // ── WiFi ──
    header(g_form, "WiFi");
    g_ta_ssid = textarea(g_form, "Network name (SSID)", s.wifi_ssid, false);
    g_ta_pass = textarea(g_form, "Password", s.wifi_password, true);
    button(g_form, "Connect & Save", save_wifi_cb);

    g_lbl_status = lv_label_create(g_form);
    lv_obj_set_width(g_lbl_status, LV_PCT(100));
    lv_label_set_long_mode(g_lbl_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(g_lbl_status, &font_montserrat_14_nor, 0);
    lv_obj_set_style_text_color(g_lbl_status, C_DIM, 0);
    lv_label_set_text(g_lbl_status, "");

    // ── Location ──
    header(g_form, "Location");
    g_lbl_loc = lv_label_create(g_form);
    lv_obj_set_width(g_lbl_loc, LV_PCT(100));
    lv_label_set_long_mode(g_lbl_loc, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(g_lbl_loc, &font_montserrat_14_nor, 0);
    lv_obj_set_style_text_color(g_lbl_loc, C_DIM, 0);
    refresh_loc_label();

    g_dd_mode = dropdown(g_form, "Mode", "Auto (public IP)\nPick a city\nCoordinates", s.loc_mode, mode_cb, 160);

    g_box_auto = box(g_form);
    button(g_box_auto, "Locate now by public IP", locate_cb);

    g_box_city = box(g_form);
    char conts[128]; size_t o = 0;
    for (int i = 0; i < CONTINENT_COUNT; i++) o += snprintf(conts + o, sizeof(conts) - o, "%s%s", i ? "\n" : "", CONTINENT_NAMES[i]);
    g_dd_cont    = dropdown(g_box_city, "Continent", conts, s.continent < CONTINENT_COUNT ? s.continent : 2, cont_cb);
    g_dd_country = dropdown(g_box_city, "Country", "-", 0, country_cb, 170);
    g_dd_city    = dropdown(g_box_city, "City", "- load -", 0, nullptr, 170);
    fill_countries(lv_dropdown_get_selected(g_dd_cont));
    // preselect saved country
    for (int i = 0; i < g_country_n; i++) if (!strcmp(COUNTRIES[g_country_map[i]].cc, s.country_cc)) { lv_dropdown_set_selected(g_dd_country, i); break; }
    button(g_box_city, "Load city list", load_cities_cb);
    g_ta_city = textarea(g_box_city, "...or type a city name", s.loc_mode == LOC_CITY ? s.city : "", false);
    button(g_box_city, "Use this city", use_city_cb);

    g_box_manual = box(g_form);
    char latb[16], lonb[16];
    snprintf(latb, sizeof(latb), "%.4f", s.man_lat); snprintf(lonb, sizeof(lonb), "%.4f", s.man_lon);
    g_ta_lat = textarea(g_box_manual, "Latitude  e.g. 59.9139", s.man_lat != 0 ? latb : "", false);
    g_ta_lon = textarea(g_box_manual, "Longitude e.g. 10.7522", s.man_lon != 0 ? lonb : "", false);
    button(g_box_manual, "Use coordinates", use_manual_cb);
    show_mode_boxes();

    // ── Radar ──
    header(g_form, "Radar");
    char ropts[128]; o = 0;
    for (int i = 0; i < RANGE_STEP_COUNT; i++) o += snprintf(ropts + o, sizeof(ropts) - o, "%s%d km", i ? "\n" : "", RANGE_STEPS_KM[i]);
    g_dd_range = dropdown(g_form, "Range", ropts, settings_range_index(), radar_opts_cb, 110);
    char uopts[96]; o = 0; int usel = 2;
    for (int i = 0; i < UPDATE_COUNT; i++) { o += snprintf(uopts + o, sizeof(uopts) - o, "%s%d s", i ? "\n" : "", UPDATE_STEPS[i]); if (UPDATE_STEPS[i] == s.update_s) usel = i; }
    g_dd_update = dropdown(g_form, "Update every", uopts, usel, radar_opts_cb, 110);
    g_dd_units  = dropdown(g_form, "Units", "Metric (km, m)\nAviation (nm, ft)", s.units, radar_opts_cb, 160);
    g_dd_source = dropdown(g_form, "Data source", "Auto failover\nadsb.lol\nadsb.fi", s.source, radar_opts_cb, 140);
    int asel = 3; for (int i = 0; i < 6; i++) if (ALERT_STEPS[i] == s.alert_km) asel = i;
    g_dd_alert  = dropdown(g_form, "Overhead alert", "Off\n1 km\n2 km\n3 km\n5 km\n10 km", asel, radar_opts_cb, 110);
    g_sw_alertbr = toggle(g_form, "Full brightness on alert", s.alert_bright);
    g_sw_map    = toggle(g_form, "Map underlay", s.map_underlay);
    g_sw_apt    = toggle(g_form, "Airport markers", s.airports);
    g_sw_sweep  = toggle(g_form, "Sweep animation", s.sweep);
    g_sw_trails = toggle(g_form, "Trails", s.trails);
    g_sw_labels = toggle(g_form, "Callsign labels", s.labels);
    g_sw_ground = toggle(g_form, "Hide aircraft on ground", s.hide_ground);
    g_sw_mil    = toggle(g_form, "Highlight military", s.highlight_mil);
    g_sw_auto   = toggle(g_form, "Auto range", s.auto_range);
    g_sw_night  = toggle(g_form, "Dim at night (22-07)", s.night_dim);
    g_sw_iss    = toggle(g_form, "Show the ISS", s.iss);
    g_sw_metar  = toggle(g_form, "METAR on stats page", s.metar);

    header(g_form, "Classes shown");
    static const char* CLS_NAMES[5] = {"Airliners", "Light aircraft", "Helicopters", "Military", "Other"};
    for (int i = 0; i < 5; i++) g_sw_cls[i] = toggle(g_form, CLS_NAMES[i], s.class_mask & (1 << i));

    header(g_form, "Integrations");
    lv_obj_t* note = lv_label_create(g_form);
    lv_label_set_text(note, "Full setup in the web panel: http://esp32-radar.local");
    lv_obj_set_width(note, LV_PCT(100)); lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_12, 0); lv_obj_set_style_text_color(note, C_DIM, 0);
    g_ta_watch = textarea(g_form, "Watchlist, e.g. LN-,SAS,A388", s.watchlist, false);
    g_ta_ntfy  = textarea(g_form, "ntfy.sh topic (push alerts)", s.ntfy_topic, false);
    button(g_form, "Save integrations", save_integrations_cb);

    lv_obj_t* br = labeled_row(g_form, "Brightness");
    g_sl_bright = lv_slider_create(br);
    lv_obj_set_width(g_sl_bright, 140);
    lv_slider_set_range(g_sl_bright, 10, 255);
    lv_slider_set_value(g_sl_bright, s.brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_sl_bright, lv_color_hex(0x0B2E16), 0);
    lv_obj_set_style_bg_color(g_sl_bright, C_LINE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_sl_bright, C_TXT, LV_PART_KNOB);
    lv_obj_add_event_cb(g_sl_bright, radar_opts_cb, LV_EVENT_RELEASED, nullptr);

    lv_obj_t* ver = lv_label_create(g_form);
    lv_label_set_text(ver, "ESP32 ADS-B Radar " FW_VERSION "  -  data: adsb.lol / adsb.fi, Open-Meteo, ip-api");
    lv_obj_set_width(ver, LV_PCT(100));
    lv_label_set_long_mode(ver, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ver, C_DIM, 0);

    // keyboard (hidden until a field is focused)
    g_kb = lv_keyboard_create(parent);
    lv_obj_set_size(g_kb, DISPLAY_WIDTH, KB_H);
    lv_obj_align(g_kb, LV_ALIGN_BOTTOM_MID, 0, 0);   // page content area ends above the nav bar
    lv_keyboard_set_map(g_kb, LV_KEYBOARD_MODE_TEXT_LOWER, kb_map_lc, kb_ctrl_lc);
    lv_keyboard_set_map(g_kb, LV_KEYBOARD_MODE_TEXT_UPPER, kb_map_uc, kb_ctrl_uc);
    lv_obj_set_style_text_font(g_kb, &font_montserrat_14_nor, 0);
    lv_obj_set_style_bg_color(g_kb, lv_color_black(), 0);
    lv_obj_set_style_bg_color(g_kb, lv_color_hex(0x07200F), LV_PART_ITEMS);
    lv_obj_set_style_text_color(g_kb, C_TXT, LV_PART_ITEMS);
    lv_obj_set_style_border_color(g_kb, C_LINE, LV_PART_ITEMS);
    lv_obj_set_style_border_width(g_kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(g_kb, C_LINE, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(g_kb, C_LINE, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(g_kb, kb_cb, LV_EVENT_ALL, nullptr);
    lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
}

void ui_settings_set_status(const char* msg) { status(msg); }

void ui_settings_debug(char what) {
    if (what == 'K') kb_show(g_ta_ssid, false);
    if (what == 'O') lv_dropdown_open(g_dd_country);
}

void ui_settings_cities_loaded() {
    const CityList& cl = net_cities();
    if (cl.count == 0) { lv_dropdown_set_options(g_dd_city, "- none -"); return; }
    static char opts[MAX_CITIES * CITY_NAME_LEN + 8];
    size_t o = 0;
    for (int i = 0; i < cl.count; i++) {
        int w = snprintf(opts + o, sizeof(opts) - o, "%s%s", i ? "\n" : "", cl.names[i]);
        if (w < 0 || o + w >= sizeof(opts)) break;
        o += w;
    }
    lv_dropdown_set_options(g_dd_city, opts);
    lv_dropdown_set_selected(g_dd_city, 0);
}

void ui_settings_location_changed() {
    const Settings& s = settings_get();
    refresh_loc_label();
    lv_dropdown_set_selected(g_dd_mode, s.loc_mode);
    if (s.loc_mode == LOC_CITY && s.country_cc[0]) {
        // select the continent + country matching the saved ISO code
        for (size_t i = 0; i < COUNTRY_COUNT; i++) {
            if (strcmp(COUNTRIES[i].cc, s.country_cc)) continue;
            if (lv_dropdown_get_selected(g_dd_cont) != COUNTRIES[i].continent) {
                lv_dropdown_set_selected(g_dd_cont, COUNTRIES[i].continent);
                fill_countries(COUNTRIES[i].continent);
            }
            for (int k = 0; k < g_country_n; k++) if (g_country_map[k] == (int)i) { lv_dropdown_set_selected(g_dd_country, k); break; }
            break;
        }
        if (s.city[0]) lv_textarea_set_text(g_ta_city, s.city);
    }
    show_mode_boxes();
}
