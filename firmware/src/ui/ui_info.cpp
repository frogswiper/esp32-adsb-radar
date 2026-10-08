#include "ui_info.h"
#include "ui_main.h"
#include "../board_config.h"
#include "../settings.h"
#include "../adsb.h"
#include "../net_task.h"
#include "../ntp.h"
#include "../countries.h"
#include "../web_server.h"
#include "../mqtt.h"
#include "../extras.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

LV_FONT_DECLARE(font_montserrat_14_nor);
static lv_obj_t* g_txt = nullptr;

static void locate_cb(lv_event_t*) { net_send(NC_LOCATE_IP); }
static void reboot_cb(lv_event_t*) { ESP.restart(); }
static void reset_cb(lv_event_t*)  { settings_factory_reset(); ESP.restart(); }
static void ota_cb(lv_event_t*)    { web_server_arm_ota(600); ui_info_refresh(); }

static lv_obj_t* mk_btn(lv_obj_t* parent, const char* txt, lv_event_cb_t cb) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_size(b, LV_PCT(100), 36);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x07200F), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0x1FA84A), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(0x3DF25A), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

void ui_info_build(lv_obj_t* parent) {
    lv_obj_t* col = lv_obj_create(parent);
    lv_obj_set_size(col, DISPLAY_WIDTH, CONTENT_HEIGHT);
    lv_obj_set_pos(col, 0, 0);
    lv_obj_set_style_bg_color(col, lv_color_black(), 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_radius(col, 0, 0);
    lv_obj_set_style_pad_all(col, 8, 0);
    lv_obj_set_style_pad_row(col, 8, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);
    lv_obj_set_style_bg_color(col, lv_color_hex(0x1FA84A), LV_PART_SCROLLBAR);

    lv_obj_t* h = lv_label_create(col);
    lv_label_set_text(h, "Status");
    lv_obj_set_style_text_font(h, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(h, lv_color_hex(0x3DF25A), 0);

    g_txt = lv_label_create(col);
    lv_obj_set_width(g_txt, LV_PCT(100));
    lv_label_set_long_mode(g_txt, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(g_txt, &font_montserrat_14_nor, 0);
    lv_obj_set_style_text_color(g_txt, lv_color_hex(0x33D650), 0);
    lv_label_set_text(g_txt, "");

    mk_btn(col, "Allow firmware update (10 min)", ota_cb);
    mk_btn(col, "Re-locate by public IP", locate_cb);
    mk_btn(col, "Reboot", reboot_cb);
    mk_btn(col, "Reset settings (keeps WiFi)", reset_cb);
}

void ui_info_refresh() {
    if (!g_txt) return;
    const Settings& s = settings_get();
    const LocalInfo& li = net_local_info();
    char st[96]; net_status(st, sizeof(st));
    char ip[20]; net_wifi_ip(ip, sizeof(ip));
    adsb_lock();
    AdsbStats stt = adsb_stats();
    adsb_unlock();
    uint32_t age = stt.last_ok_ms ? (millis() - stt.last_ok_ms) / 1000 : 0;
    struct tm t = ntp_get_local_time();
    static const char* modes[] = {"auto (public IP)", "city", "coordinates"};
    char buf[900];
    snprintf(buf, sizeof(buf),
        "Location: %s\n%.4f, %.4f  (%s)\n"
        "Timezone: %s  UTC%+d\nLocal time: %02d:%02d:%02d\n\n"
        "Source: %s   poll %lus ago, %lu ms\n"
        "Polls: %lu ok / %lu failed   HTTP %d\n"
        "Tracked: %d aircraft, %d emergency\n"
        "Range %d km, update every %d s\n\n"
        "WiFi: %s  %s  %d dBm\n"
        "Status: %s\n\n"
        "Web panel: http://%s.local  (%s)\nOTA: %s   MQTT: %s\n"
        "Heap %u KB   PSRAM %u KB free\n"
        "Uptime %lu min   FW " FW_VERSION,
        s.place[0] ? s.place : "-", s.lat, s.lon, modes[s.loc_mode > 2 ? 0 : s.loc_mode],
        li.tz_name[0] ? li.tz_name : "-", s.utc_offset_seconds / 3600, t.tm_hour, t.tm_min, t.tm_sec,
        stt.source[0] ? stt.source : "-", (unsigned long)age, (unsigned long)stt.last_latency_ms,
        (unsigned long)(stt.total_fetches - stt.total_failures), (unsigned long)stt.total_failures, stt.last_http,
        stt.count, stt.emergencies, s.range_km, s.update_s,
        net_wifi_connected() ? "connected" : "down", ip, net_wifi_connected() ? net_wifi_rssi() : 0,
        st,
        web_server_hostname(), web_server_started() ? ip : "not started", web_server_ota_armed() ? "ARMED" : "locked", settings_get().mqtt_uri[0] ? (mqtt_connected() ? "connected" : "connecting") : "off",
        (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
        (unsigned long)(millis() / 60000));
    lv_label_set_text(g_txt, buf);
}
