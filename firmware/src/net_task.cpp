#include "net_task.h"
#include "settings.h"
#include "adsb.h"
#include "ntp.h"
#include "notify.h"
#include "extras.h"
#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

static QueueHandle_t     g_q      = nullptr;
static SemaphoreHandle_t g_mtx    = nullptr;
static volatile uint32_t g_events = 0;
static volatile bool     g_busy   = false;
static volatile int      g_fetch_queued = 0;
static char              g_status[96] = "Starting...";
static CityList          g_cities;
static LocalInfo         g_local = {};

static void set_status(const char* s) {
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    strlcpy(g_status, s, sizeof(g_status));
    g_events |= EV_STATUS;
    xSemaphoreGive(g_mtx);
    Serial.printf("[NET] %s\n", s);
}
static void post(uint32_t ev) {
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    g_events |= ev;
    xSemaphoreGive(g_mtx);
}

static bool do_connect_wifi() {
    Settings& s = settings_get();
    if (!strlen(s.wifi_ssid)) { set_status("No WiFi credentials"); return false; }
    char m[96]; snprintf(m, sizeof(m), "Connecting to %s...", s.wifi_ssid); set_status(m);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(true, false);
    delay(100);
    WiFi.begin(s.wifi_ssid, s.wifi_password);
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) delay(200);
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(m, sizeof(m), "WiFi OK  %s", WiFi.localIP().toString().c_str());
        set_status(m);
        return true;
    }
    set_status("WiFi failed - check SSID/password");
    return false;
}

static void finish_location(const char* label) {
    Settings& s = settings_get();
    strlcpy(s.place, label, sizeof(s.place));
    settings_save();
    adsb_clear();
    char m[96]; snprintf(m, sizeof(m), "Location: %s (%.3f, %.3f)", s.place, s.lat, s.lon);
    set_status(m);
    post(EV_LOCATED);
}

static void net_task(void*) {
    NetMsg msg;
    for (;;) {
        if (xQueueReceive(g_q, &msg, portMAX_DELAY) != pdTRUE) continue;
        g_busy = true;
        Settings& s = settings_get();
        switch (msg.cmd) {
        case NC_CONNECT_WIFI:
            post(do_connect_wifi() ? EV_WIFI_OK : EV_WIFI_FAIL);
            break;

        case NC_LOCATE_IP: {
            set_status("Locating by public IP...");
            float lat, lon; char label[48];
            if (geo_locate_by_ip(lat, lon, label, sizeof(label))) {
                s.lat = lat; s.lon = lon; s.loc_mode = LOC_AUTO_IP;
                finish_location(label);
            } else { set_status("IP geolocation failed"); post(EV_LOCATE_FAIL); }
            break;
        }
        case NC_FETCH_CITIES: {
            char m[96]; snprintf(m, sizeof(m), "Loading cities for %s...", msg.arg1); set_status(m);
            CityList tmp;
            bool ok = geo_fetch_cities(msg.arg1, tmp);
            xSemaphoreTake(g_mtx, portMAX_DELAY);
            if (ok) g_cities = tmp; else g_cities.count = 0;
            xSemaphoreGive(g_mtx);
            if (ok) { snprintf(m, sizeof(m), "%d cities loaded - pick one", tmp.count); set_status(m); post(EV_CITIES); }
            else    { set_status("City list unavailable - type the city instead"); post(EV_CITIES_FAIL); }
            break;
        }
        case NC_GEOCODE: {
            char m[96]; snprintf(m, sizeof(m), "Looking up %s...", msg.arg1); set_status(m);
            float lat, lon; char label[48];
            if (geo_geocode(msg.arg1, msg.arg2, lat, lon, label, sizeof(label))) {
                s.lat = lat; s.lon = lon; s.loc_mode = LOC_CITY;
                strlcpy(s.city, msg.arg1, sizeof(s.city));
                strlcpy(s.country_cc, msg.arg2, sizeof(s.country_cc));
                finish_location(label);
            } else { set_status("City not found"); post(EV_LOCATE_FAIL); }
            break;
        }
        case NC_APPLY_MANUAL: {
            s.lat = msg.f1; s.lon = msg.f2; s.man_lat = msg.f1; s.man_lon = msg.f2; s.loc_mode = LOC_MANUAL;
            char label[48]; snprintf(label, sizeof(label), "%.3f, %.3f", msg.f1, msg.f2);
            finish_location(label);
            break;
        }
        case NC_LOCAL_INFO: {
            LocalInfo li = {};
            if (geo_fetch_local_info(s.lat, s.lon, li)) {
                xSemaphoreTake(g_mtx, portMAX_DELAY); g_local = li; xSemaphoreGive(g_mtx);
                if (s.utc_offset_seconds != li.utc_offset_seconds) { s.utc_offset_seconds = li.utc_offset_seconds; settings_save(); }
                ntp_apply_offset(li.utc_offset_seconds);
                if (!ntp_is_synced()) ntp_sync();
                post(EV_LOCAL_INFO);
            }
            break;
        }
        case NC_NOTIFY:
            notify_deliver(msg.arg1, msg.text, (NotifyKind)msg.kind);
            break;
        case NC_ISS: {
            IssState iss = extras_iss();
            if (extras_fetch_iss(iss)) { extras_set_iss(iss); post(EV_ISS); }
            break;
        }
        case NC_METAR: {
            MetarState m = extras_metar();
            if (extras_fetch_metar(msg.arg1, m)) { extras_set_metar(m); post(EV_METAR); Serial.printf("[METAR] %s\n", m.raw); }
            else Serial.printf("[METAR] fetch failed for %s\n", msg.arg1);
            break;
        }
        case NC_FETCH_AIRCRAFT: {
            if (WiFi.status() == WL_CONNECTED && settings_has_location()) {
                int fetch_km = s.range_km;
                if (s.auto_range && fetch_km < 200) fetch_km = 200;
                bool ok = adsb_fetch(s.lat, s.lon, fetch_km);
                post(ok ? EV_AIRCRAFT : EV_AIRCRAFT_FAIL);
                if (ok) { adsb_fetch_routes(); post(EV_AIRCRAFT); }
            }
            if (g_fetch_queued > 0) g_fetch_queued--;
            break;
        }
        }
        g_busy = false;
    }
}

void net_task_start() {
    g_q   = xQueueCreate(8, sizeof(NetMsg));
    g_mtx = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(net_task, "net", 20480, nullptr, 1, nullptr, 0);
}

bool net_send(NetCmd cmd, const char* a1, const char* a2, float f1, float f2) {
    NetMsg m = {}; m.cmd = cmd; m.f1 = f1; m.f2 = f2;
    if (a1) strlcpy(m.arg1, a1, sizeof(m.arg1));
    if (a2) strlcpy(m.arg2, a2, sizeof(m.arg2));
    if (cmd == NC_FETCH_AIRCRAFT) {
        if (g_fetch_queued > 0) return false;      // one in flight is enough
        g_fetch_queued++;
    }
    if (xQueueSend(g_q, &m, 0) != pdTRUE) { if (cmd == NC_FETCH_AIRCRAFT) g_fetch_queued--; return false; }
    return true;
}
bool net_send_notify(const char* title, const char* message, uint8_t kind) {
    NetMsg m = {}; m.cmd = NC_NOTIFY; m.kind = kind;
    strlcpy(m.arg1, title, sizeof(m.arg1)); strlcpy(m.text, message, sizeof(m.text));
    return xQueueSend(g_q, &m, 0) == pdTRUE;
}
bool net_busy()           { return g_busy; }
bool net_fetch_pending()  { return g_fetch_queued > 0; }
uint32_t net_take_events() {
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    uint32_t e = g_events; g_events = 0;
    xSemaphoreGive(g_mtx);
    return e;
}
void net_status(char* buf, size_t n) {
    xSemaphoreTake(g_mtx, portMAX_DELAY); strlcpy(buf, g_status, n); xSemaphoreGive(g_mtx);
}
bool net_wifi_connected() { return WiFi.status() == WL_CONNECTED; }
int  net_wifi_rssi()      { return WiFi.RSSI(); }
void net_wifi_ip(char* buf, size_t n) { strlcpy(buf, WiFi.localIP().toString().c_str(), n); }
const CityList&  net_cities()     { return g_cities; }
const LocalInfo& net_local_info() { return g_local; }
