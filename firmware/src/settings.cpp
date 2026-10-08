#include "settings.h"
#include <Preferences.h>
#include <string.h>

static Preferences prefs;
static Settings    g;
static const char* NS = "radar1";

const uint16_t RANGE_STEPS_KM[] = {10, 25, 50, 75, 100, 150, 200, 300, 400};
const int      RANGE_STEP_COUNT = sizeof(RANGE_STEPS_KM) / sizeof(RANGE_STEPS_KM[0]);

void settings_load() {
    memset(&g, 0, sizeof(g));
    prefs.begin(NS, true);
    prefs.getString("ssid",  g.wifi_ssid,     sizeof(g.wifi_ssid));
    prefs.getString("pass",  g.wifi_password, sizeof(g.wifi_password));
    prefs.getString("place", g.place,         sizeof(g.place));
    prefs.getString("cc",    g.country_cc,    sizeof(g.country_cc));
    prefs.getString("city",  g.city,          sizeof(g.city));
    g.loc_mode   = prefs.getUChar("locmode", LOC_AUTO_IP);
    g.continent  = prefs.getUChar("cont",    2);          // Europe
    g.lat        = prefs.getFloat("lat", 0.0f);
    g.lon        = prefs.getFloat("lon", 0.0f);
    g.man_lat    = prefs.getFloat("mlat", 0.0f);
    g.man_lon    = prefs.getFloat("mlon", 0.0f);
    g.range_km   = prefs.getUShort("range", 100);
    g.update_s   = prefs.getUShort("upd", 5);
    g.units      = prefs.getUChar("units", UNITS_METRIC);
    g.source     = prefs.getUChar("src", SRC_AUTO);
    g.sweep      = prefs.getBool("sweep", true);
    g.trails     = prefs.getBool("trails", true);
    g.labels     = prefs.getBool("labels", true);
    g.hide_ground= prefs.getBool("hidegnd", false);
    g.auto_range = prefs.getBool("autorng", false);
    g.night_dim  = prefs.getBool("night", true);
    g.highlight_mil = prefs.getBool("mil", true);
    g.brightness = prefs.getUChar("bright", 200);
    g.alert_km   = prefs.getUChar("alertkm", 3);
    g.alert_bright = prefs.getBool("alertbr", true);
    g.map_underlay = prefs.getBool("map", true);
    g.airports   = prefs.getBool("apt", true);
    prefs.getString("watch", g.watchlist,   sizeof(g.watchlist));
    prefs.getString("ntfy",  g.ntfy_topic,  sizeof(g.ntfy_topic));
    prefs.getString("hook",  g.webhook_url, sizeof(g.webhook_url));
    prefs.getString("mqtt",  g.mqtt_uri,    sizeof(g.mqtt_uri));
    prefs.getString("local", g.local_url,   sizeof(g.local_url));
    prefs.getString("ppass", g.panel_pass,  sizeof(g.panel_pass));
    g.class_mask = prefs.getUChar("cmask", 0x1F);
    g.iss        = prefs.getBool("iss", true);
    g.metar      = prefs.getBool("metar", true);
    g.notify_emergency = prefs.getBool("nemg", true);
    g.notify_watch     = prefs.getBool("nwatch", true);
    g.notify_alert     = prefs.getBool("nalert", true);
    g.utc_offset_seconds = prefs.getInt("utcoff", 0);
    prefs.end();

    // First boot after the DeskClock firmware: inherit its WiFi credentials + city.
    if (!g.wifi_ssid[0] && prefs.begin("dsk2", true)) {
        prefs.getString("ssid", g.wifi_ssid,     sizeof(g.wifi_ssid));
        prefs.getString("pass", g.wifi_password, sizeof(g.wifi_password));
        prefs.end();
        if (g.wifi_ssid[0]) settings_save();
    }
    if (g.update_s < 2) g.update_s = 2;
    if (g.range_km < 10) g.range_km = 10;
    if (g.brightness < 10) g.brightness = 10;
}

void settings_save() {
    prefs.begin(NS, false);
    prefs.putString("ssid",  g.wifi_ssid);
    prefs.putString("pass",  g.wifi_password);
    prefs.putString("place", g.place);
    prefs.putString("cc",    g.country_cc);
    prefs.putString("city",  g.city);
    prefs.putUChar("locmode", g.loc_mode);
    prefs.putUChar("cont",    g.continent);
    prefs.putFloat("lat",  g.lat);
    prefs.putFloat("lon",  g.lon);
    prefs.putFloat("mlat", g.man_lat);
    prefs.putFloat("mlon", g.man_lon);
    prefs.putUShort("range", g.range_km);
    prefs.putUShort("upd",   g.update_s);
    prefs.putUChar("units",  g.units);
    prefs.putUChar("src",    g.source);
    prefs.putBool("sweep",   g.sweep);
    prefs.putBool("trails",  g.trails);
    prefs.putBool("labels",  g.labels);
    prefs.putBool("hidegnd", g.hide_ground);
    prefs.putBool("autorng", g.auto_range);
    prefs.putBool("night",   g.night_dim);
    prefs.putBool("mil",     g.highlight_mil);
    prefs.putUChar("bright", g.brightness);
    prefs.putUChar("alertkm", g.alert_km);
    prefs.putBool("alertbr", g.alert_bright);
    prefs.putBool("map",     g.map_underlay);
    prefs.putBool("apt",     g.airports);
    prefs.putString("watch", g.watchlist);
    prefs.putString("ntfy",  g.ntfy_topic);
    prefs.putString("hook",  g.webhook_url);
    prefs.putString("mqtt",  g.mqtt_uri);
    prefs.putString("local", g.local_url);
    prefs.putString("ppass", g.panel_pass);
    prefs.putUChar("cmask",  g.class_mask);
    prefs.putBool("iss",     g.iss);
    prefs.putBool("metar",   g.metar);
    prefs.putBool("nemg",    g.notify_emergency);
    prefs.putBool("nwatch",  g.notify_watch);
    prefs.putBool("nalert",  g.notify_alert);
    prefs.putInt("utcoff",   g.utc_offset_seconds);
    prefs.end();
}

Settings& settings_get()     { return g; }
bool settings_has_wifi()     { return strlen(g.wifi_ssid) > 0; }
bool settings_has_location() { return g.lat != 0.0f || g.lon != 0.0f; }

int settings_range_index() {
    int best = 0;
    for (int i = 0; i < RANGE_STEP_COUNT; i++)
        if (RANGE_STEPS_KM[i] <= g.range_km) best = i;
    return best;
}
void settings_set_range_index(int idx) {
    if (idx < 0) idx = 0;
    if (idx >= RANGE_STEP_COUNT) idx = RANGE_STEP_COUNT - 1;
    g.range_km = RANGE_STEPS_KM[idx];
}

void settings_factory_reset() {
    char ssid[64], pass[64];
    strlcpy(ssid, g.wifi_ssid, sizeof(ssid)); strlcpy(pass, g.wifi_password, sizeof(pass));
    prefs.begin(NS, false); prefs.clear(); prefs.end();
    settings_load();
    strlcpy(g.wifi_ssid, ssid, sizeof(g.wifi_ssid)); strlcpy(g.wifi_password, pass, sizeof(g.wifi_password));
    settings_save();
}
