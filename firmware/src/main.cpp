/**
 * ESP32 ADS-B Radar — JC4827W543 (ESP32-S3, 4.3" 272×480 portrait, GT911 touch)
 *
 * Live aircraft radar fed by public ADS-B aggregators (adsb.lol / adsb.fi), centred on
 * the device's public-IP location or a user-chosen city / coordinate. All network I/O
 * runs on core 0 (net_task); LVGL runs here on core 1.
 */
#include <Arduino.h>
#include "display.h"
#include "settings.h"
#include "adsb.h"
#include "net_task.h"
#include "ntp.h"
#include "ui/ui_main.h"
#include "ui/ui_radar.h"
#include "ui/ui_list.h"
#include "ui/ui_info.h"
#include "ui/ui_settings.h"
#include "ui/ui_detail.h"
#include "ui/ui_stats.h"
#include "web_server.h"
#include "mqtt.h"
#include "notify.h"
#include "stats.h"
#include "extras.h"
#include <lvgl.h>

// Debug: 'S' on the serial port dumps the screen as raw RGB565 ("SNAP w h bytes\n" + data).
static void serial_commands() {
    while (Serial.available()) {
        int c = Serial.read();
        if (c == 'S') {
            lv_img_dsc_t* snap = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
            if (!snap) { Serial.println("SNAP failed"); continue; }
            size_t n = (size_t)snap->header.w * snap->header.h * sizeof(lv_color_t);
            Serial.printf("SNAP %d %d %u\n", snap->header.w, snap->header.h, (unsigned)n);
            Serial.write(snap->data, n);
            Serial.flush();
            lv_snapshot_free(snap);
        } else if (c == 'R') {
            ESP.restart();
        } else if (c == 'L') {
            net_send(NC_LOCATE_IP);
        } else if (c == 'C') {           // C<city>,<cc>\n  → set location by city
            char line[64]; size_t n = Serial.readBytesUntil('\n', line, sizeof(line) - 1); line[n] = 0;
            char* comma = strchr(line, ',');
            if (comma) { *comma = 0; net_send(NC_GEOCODE, line, comma + 1); }
        } else if (c == 'D') {
            settings_factory_reset(); ESP.restart();
        } else if (c == 'T') {           // open detail page for the nearest aircraft
            adsb_lock(); int i = adsb_stats().nearest; char hx[8] = ""; if (i >= 0) strlcpy(hx, adsb_list()[i].hex, 8); adsb_unlock();
            ui_detail_show(hx[0] ? hx : nullptr); ui_main_goto_tab(PAGE_DETAIL);
        } else if (c == 'K' || c == 'O') {
            ui_main_goto_tab(PAGE_SETTINGS); ui_settings_debug(c);
        } else if (c >= '1' && c <= '5') {
            ui_main_goto_tab(c - '1');
        }
    }
}

static uint32_t t_last_fetch = 0, t_last_wx = 0, t_last_wifi = 0, t_last_info = 0, t_last_dim = 0, t_last_iss = 0, t_last_metar = 0;
static int      wifi_backoff_s = 10;
static bool     alert_bright_on = false;
static bool     wifi_was_up = false;

static void apply_brightness() {
    const Settings& s = settings_get();
    if (alert_bright_on) return;
    uint8_t b = s.brightness;
    if (s.night_dim && ntp_is_synced()) {
        struct tm t = ntp_get_local_time();
        if (t.tm_hour >= 22 || t.tm_hour < 7) b = (uint8_t)max(8, (int)(b * 0.2f));
    }
    display_set_brightness(b);
}

// Auto range: smallest ring range that holds the 3 nearest aircraft (min 25 km).
static void auto_range() {
    Settings& s = settings_get();
    if (!s.auto_range) return;
    adsb_lock();
    Aircraft* ac = adsb_list(); int n = adsb_count();
    float d[3] = {1e9f, 1e9f, 1e9f}; int cnt = 0;
    for (int i = 0; i < n; i++) {
        if (!ac[i].hex[0] || (s.hide_ground && ac[i].on_ground)) continue;
        cnt++;
        float v = ac[i].dist_km;
        for (int k = 0; k < 3; k++) if (v < d[k]) { for (int m = 2; m > k; m--) d[m] = d[m - 1]; d[k] = v; break; }
    }
    adsb_unlock();
    if (cnt == 0) return;
    float need = d[min(cnt, 3) - 1] * 1.15f;
    int idx = 1;   // 25 km minimum
    while (idx < RANGE_STEP_COUNT - 1 && RANGE_STEPS_KM[idx] < need) idx++;
    if (RANGE_STEPS_KM[idx] != s.range_km) {
        s.range_km = RANGE_STEPS_KM[idx];
        ui_radar_range_changed();
    }
}

static void request_fetch() {
    if (net_wifi_connected() && settings_has_location()) {
        net_send(NC_FETCH_AIRCRAFT);
        t_last_fetch = millis();
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("[Radar] ESP32 ADS-B Radar " FW_VERSION " starting");

    display_init();
    settings_load();
    {
        const Settings& s = settings_get();
        Serial.printf("[Radar] settings: ssid='%s' loc_mode=%d place='%s' lat=%.3f lon=%.3f range=%d upd=%d units=%d src=%d auto=%d\n",
                      s.wifi_ssid, s.loc_mode, s.place, s.lat, s.lon, s.range_km, s.update_s, s.units, s.source, s.auto_range);
    }
    adsb_init();
    notify_init();
    stats_init();
    net_task_start();
    ui_main_init();
    display_set_brightness(settings_get().brightness);

    if (settings_has_wifi()) {
        net_send(NC_CONNECT_WIFI);
    } else {
        ui_main_goto_tab(PAGE_SETTINGS);
        ui_settings_set_status("Enter WiFi to get started. Location is found automatically from your public IP.");
    }
}

void loop() {
    lv_timer_handler();
    serial_commands();
    uint32_t now = millis();
    Settings& s = settings_get();

    // ── events from the network task ──
    uint32_t ev = net_take_events();
    if (ev & EV_STATUS) {
        char st[96]; net_status(st, sizeof(st));
        ui_settings_set_status(st);
        if (!net_local_info().valid) ui_radar_notify_weather();
    }
    if (ev & EV_WIFI_OK) {
        wifi_was_up = true; wifi_backoff_s = 10;
        web_server_start();
        if (s.loc_mode == LOC_AUTO_IP || !settings_has_location()) net_send(NC_LOCATE_IP);
        else { net_send(NC_LOCAL_INFO); request_fetch(); }
    }
    if (ev & EV_WIFI_FAIL) {
        if (ui_main_active_tab() == 0 && !settings_has_wifi()) ui_main_goto_tab(PAGE_SETTINGS);
    }
    if (ev & EV_LOCATED) {
        ui_settings_location_changed();
        ui_radar_range_changed();
        net_send(NC_LOCAL_INFO);
        request_fetch();
        t_last_wx = now;
    }
    if (ev & EV_LOCATE_FAIL) {
        if (!settings_has_location()) ui_main_goto_tab(PAGE_SETTINGS);
    }
    if (ev & EV_CITIES)      ui_settings_cities_loaded();
    if (ev & EV_CITIES_FAIL) ui_settings_cities_loaded();
    if (ev & EV_LOCAL_INFO)  { ui_radar_notify_weather(); apply_brightness(); }
    if (ev & (EV_AIRCRAFT | EV_AIRCRAFT_FAIL)) {
        if (ev & EV_AIRCRAFT) { auto_range(); stats_tick(); mqtt_publish_state(); }
        ui_radar_notify_data();
        if (ui_main_active_tab() == PAGE_LIST) ui_list_refresh();
        if (ui_main_active_tab() == PAGE_STATS) ui_stats_refresh();
        // overhead alerts → notification (once per aircraft per 30 min)
        adsb_lock();
        { Aircraft* ac = adsb_list(); int n = adsb_count();
          for (int i = 0; i < n; i++) if (ac[i].hex[0] && ac[i].alert && adsb_visible(ac[i])) {
              char title[40], msg[96]; const char* who = ac[i].flight[0] ? ac[i].flight : ac[i].hex;
              snprintf(title, sizeof(title), "Flyover %s", who);
              snprintf(msg, sizeof(msg), "%s %s passes in ~%.0f min at %.1f km, look %s", ac[i].type, ac[i].reg, ac[i].cpa_min, ac[i].cpa_km, adsb_compass16(ac[i].bearing));
              if (notify_event(NK_OVERHEAD, ac[i].hex, title, msg)) stats_note_alert();
          } }
        adsb_unlock();
    }
    if (ev & EV_ISS) {
        IssState iss = extras_iss();
        iss.dist_km = geo_distance_km(s.lat, s.lon, iss.lat, iss.lon); iss.bearing = geo_bearing_deg(s.lat, s.lon, iss.lat, iss.lon);
        extras_set_iss(iss); ui_radar_notify_data();
    }
    if (ev & EV_METAR) { if (ui_main_active_tab() == PAGE_STATS) ui_stats_refresh(); }

    // ── scheduled work ──
    if (ui_main_consume_fetch_request()) request_fetch();

    if (net_wifi_connected() && settings_has_location()) {
        if (now - t_last_fetch >= (uint32_t)s.update_s * 1000UL && !net_fetch_pending()) request_fetch();
        if (now - t_last_wx >= 30UL * 60UL * 1000UL) { net_send(NC_LOCAL_INFO); t_last_wx = now; }
        if (s.iss && now - t_last_iss >= 60000UL && !net_busy()) { net_send(NC_ISS); t_last_iss = now; }
        if (s.metar && (t_last_metar == 0 || now - t_last_metar >= 15UL * 60UL * 1000UL) && !net_busy()) {
            const char* stn = extras_metar_station(s.lat, s.lon);
            if (stn[0]) net_send(NC_METAR, stn);
            t_last_metar = now;
        }
    }
    web_server_loop();
    mqtt_loop();
    stats_save_if_due();

    // WiFi watchdog with backoff
    if (settings_has_wifi() && !net_wifi_connected() && !net_busy() && now - t_last_wifi >= (uint32_t)wifi_backoff_s * 1000UL) {
        t_last_wifi = now;
        if (wifi_was_up || wifi_backoff_s > 10) {
            net_send(NC_CONNECT_WIFI);
            wifi_backoff_s = min(wifi_backoff_s * 2, 120);
        } else wifi_backoff_s = 20;   // first failure: give the user time on the settings page
    }

    if (ui_main_active_tab() == PAGE_INFO && now - t_last_info >= 2000) { ui_info_refresh(); t_last_info = now; }
    if (ui_main_active_tab() == PAGE_DETAIL && now - t_last_info >= 1000) { ui_detail_refresh(); t_last_info = now; }

    // overhead alert → full backlight while active
    {
        adsb_lock(); bool any = adsb_stats().alerts > 0; adsb_unlock();
        bool want = any && s.alert_bright;
        if (want != alert_bright_on) {
            alert_bright_on = want;
            if (want) display_set_brightness(255); else apply_brightness();
        }
    }
    if (now - t_last_dim >= 60000) { apply_brightness(); t_last_dim = now; }

    delay(5);
}
