#include "mqtt.h"
#include "settings.h"
#include "adsb.h"
#include "net_task.h"
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

static WiFiClient   g_net;
static PubSubClient g_mq(g_net);
static uint32_t     g_last_try = 0, g_last_pub = 0;
static bool         g_discovered = false;
static char         g_host[64], g_user[32], g_pass[32]; static int g_port = 1883;
static char         g_devid[20];

static bool parse_uri() {
    const Settings& s = settings_get();
    const char* u = s.mqtt_uri;
    if (strncmp(u, "mqtt://", 7)) return false;
    u += 7;
    const char* at = strrchr(u, '@');
    g_user[0] = g_pass[0] = 0;
    if (at) {
        char cred[64]; strlcpy(cred, u, min((size_t)(at - u + 1), sizeof(cred)));
        char* colon = strchr(cred, ':');
        if (colon) { *colon = 0; strlcpy(g_pass, colon + 1, sizeof(g_pass)); }
        strlcpy(g_user, cred, sizeof(g_user));
        u = at + 1;
    }
    strlcpy(g_host, u, sizeof(g_host));
    char* colon = strchr(g_host, ':');
    g_port = 1883;
    if (colon) { *colon = 0; g_port = atoi(colon + 1); }
    return g_host[0] != 0;
}

static void discovery() {
    char topic[96], payload[400];
    struct { const char* id; const char* name; const char* unit; const char* tpl; const char* icon; } sensors[] = {
        {"nearest",      "Nearest aircraft",      "",   "{{ value_json.nearest }}",         "mdi:airplane"},
        {"nearest_dist", "Nearest aircraft distance", "km", "{{ value_json.nearest_km }}",  "mdi:map-marker-distance"},
        {"count",        "Aircraft tracked",      "",   "{{ value_json.count }}",           "mdi:radar"},
        {"alerts",       "Overhead alerts",       "",   "{{ value_json.alerts }}",          "mdi:bell-alert"},
        {"emergencies",  "Emergency squawks",     "",   "{{ value_json.emergencies }}",     "mdi:alert"},
        {"highest",      "Highest aircraft",      "",   "{{ value_json.highest }}",         "mdi:arrow-up-bold"},
    };
    for (auto& se : sensors) {
        snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/%s/config", g_devid, se.id);
        snprintf(payload, sizeof(payload),
            "{\"name\":\"%s\",\"uniq_id\":\"%s_%s\",\"stat_t\":\"esp32radar/%s/state\",\"val_tpl\":\"%s\",%s%s%s\"ic\":\"%s\","
            "\"dev\":{\"ids\":[\"%s\"],\"name\":\"ESP32 ADS-B Radar\",\"mf\":\"frogswiper\",\"mdl\":\"JC4827W543\",\"sw\":\"" FW_VERSION "\"}}",
            se.name, g_devid, se.id, g_devid, se.tpl, se.unit[0] ? "\"unit_of_meas\":\"" : "", se.unit, se.unit[0] ? "\"," : "", se.icon, g_devid);
        g_mq.publish(topic, payload, true);
    }
    g_discovered = true;
}

bool mqtt_connected() { return g_mq.connected(); }

void mqtt_publish_state() {
    if (!g_mq.connected()) return;
    const Settings& s = settings_get();
    adsb_lock();
    const AdsbStats& st = adsb_stats();
    Aircraft* ac = adsb_list();
    char nearest[12] = "-", highest[12] = "-"; float nd = 0;
    if (st.nearest >= 0) { const Aircraft& a = ac[st.nearest]; strlcpy(nearest, a.flight[0] ? a.flight : a.hex, sizeof(nearest)); nd = a.dist_km; }
    if (st.highest >= 0) { const Aircraft& a = ac[st.highest]; strlcpy(highest, a.flight[0] ? a.flight : a.hex, sizeof(highest)); }
    char payload[256];
    snprintf(payload, sizeof(payload), "{\"nearest\":\"%s\",\"nearest_km\":%.1f,\"count\":%d,\"alerts\":%d,\"emergencies\":%d,\"highest\":\"%s\",\"range_km\":%d,\"place\":\"%s\"}",
             nearest, nd, st.count, st.alerts, st.emergencies, highest, s.range_km, s.place);
    adsb_unlock();
    char topic[64]; snprintf(topic, sizeof(topic), "esp32radar/%s/state", g_devid);
    g_mq.publish(topic, payload, false);
    g_last_pub = millis();
}

void mqtt_loop() {
    const Settings& s = settings_get();
    if (!s.mqtt_uri[0] || !net_wifi_connected()) return;
    if (!g_devid[0]) { uint64_t mac = ESP.getEfuseMac(); snprintf(g_devid, sizeof(g_devid), "esp32radar_%06llx", mac & 0xFFFFFFULL); }
    if (!g_mq.connected()) {
        if (millis() - g_last_try < 15000) return;
        g_last_try = millis();
        if (!parse_uri()) return;
        g_mq.setServer(g_host, g_port);
        g_mq.setBufferSize(512);
        bool ok = g_user[0] ? g_mq.connect(g_devid, g_user, g_pass) : g_mq.connect(g_devid);
        Serial.printf("[MQTT] connect %s:%d -> %s\n", g_host, g_port, ok ? "ok" : "failed");
        if (ok) { g_discovered = false; discovery(); mqtt_publish_state(); }
        return;
    }
    g_mq.loop();
    if (millis() - g_last_pub > 60000) mqtt_publish_state();
}
