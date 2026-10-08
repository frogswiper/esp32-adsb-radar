#include "stats.h"
#include "adsb.h"
#include "ntp.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

static Stats g = {};
static Preferences prefs;
static uint32_t g_last_save = 0;
static bool g_dirty = false;

// hexes already counted today (compact ring)
#define SEEN_N 600
static uint32_t g_seen[SEEN_N]; static int g_seen_n = 0, g_seen_head = 0;

static uint32_t hex_to_u32(const char* h) { return (uint32_t)strtoul(h, nullptr, 16); }
static bool seen_today(uint32_t v) { for (int i = 0; i < g_seen_n; i++) if (g_seen[i] == v) return true; return false; }
static void seen_add(uint32_t v) { g_seen[g_seen_head] = v; g_seen_head = (g_seen_head + 1) % SEEN_N; if (g_seen_n < SEEN_N) g_seen_n++; }

static void top_add(TopEntry* t, const char* key) {
    if (!key || !key[0]) return;
    for (int i = 0; i < STATS_TOP; i++) if (!strcmp(t[i].key, key)) { t[i].count++; return; }
    int weakest = -1;
    for (int i = 0; i < STATS_TOP; i++) if (!t[i].key[0]) { weakest = i; break; }
    if (weakest < 0) { weakest = 0; for (int i = 1; i < STATS_TOP; i++) if (t[i].count < t[weakest].count) weakest = i; if (t[weakest].count > 3) return; }
    strlcpy(t[weakest].key, key, sizeof(t[weakest].key)); t[weakest].count = 1;
}

static uint16_t today_key() {
    struct tm t = ntp_get_local_time();
    if (t.tm_year < 100) return g.day_key;   // clock not set yet
    return (uint16_t)((t.tm_year % 100) * 400 + t.tm_yday);
}

void stats_init() {
    prefs.begin("stats1", true);
    size_t n = prefs.getBytes("blob", &g, sizeof(g));
    prefs.end();
    if (n != sizeof(g)) memset(&g, 0, sizeof(g));
    g.boot_count++;
    g_dirty = true;
}

static void new_day() {
    memset(g.hourly, 0, sizeof(g.hourly)); g.unique_today = 0; g.max_tracked = 0;
    g.max_alt_ft = 0; g.max_alt_who[0] = 0; g.max_gs_kt = 0; g.max_gs_who[0] = 0; g.min_dist_km = 0; g.min_dist_who[0] = 0;
    g.emergencies = g.alerts = 0; memset(g.airlines, 0, sizeof(g.airlines)); memset(g.types, 0, sizeof(g.types));
    g_seen_n = 0; g_seen_head = 0;
}

void stats_tick() {
    uint16_t dk = today_key();
    if (dk != g.day_key && dk != 0) { if (g.day_key) new_day(); g.day_key = dk; }
    struct tm t = ntp_get_local_time();
    adsb_lock();
    Aircraft* ac = adsb_list(); int n = adsb_count(); int tracked = 0;
    for (int i = 0; i < n; i++) {
        Aircraft& a = ac[i];
        if (!a.hex[0]) continue;
        tracked++;
        uint32_t hv = hex_to_u32(a.hex);
        if (!seen_today(hv)) {
            seen_add(hv); g.unique_today++;
            if (t.tm_year >= 100) g.hourly[t.tm_hour % 24]++;
            const char* al = adsb_airline_name(a.flight);
            if (al[0]) { char k[8] = {a.flight[0], a.flight[1], a.flight[2], 0}; top_add(g.airlines, k); }
            top_add(g.types, a.type);
        }
        if (!a.on_ground && a.alt_ft > g.max_alt_ft) { g.max_alt_ft = a.alt_ft; strlcpy(g.max_alt_who, a.flight[0] ? a.flight : a.hex, sizeof(g.max_alt_who)); }
        if (a.gs_kt > g.max_gs_kt) { g.max_gs_kt = a.gs_kt; strlcpy(g.max_gs_who, a.flight[0] ? a.flight : a.hex, sizeof(g.max_gs_who)); }
        if (!a.on_ground && (g.min_dist_km == 0 || a.dist_km < g.min_dist_km)) { g.min_dist_km = a.dist_km; strlcpy(g.min_dist_who, a.flight[0] ? a.flight : a.hex, sizeof(g.min_dist_who)); }
    }
    adsb_unlock();
    if (tracked > g.max_tracked) g.max_tracked = tracked;
    g_dirty = true;
}

void stats_note_alert()     { g.alerts++; g_dirty = true; }
void stats_note_emergency() { g.emergencies++; g_dirty = true; }
const Stats& stats_get()    { return g; }

void stats_save_if_due() {
    if (!g_dirty || millis() - g_last_save < 5UL * 60UL * 1000UL) return;
    prefs.begin("stats1", false); prefs.putBytes("blob", &g, sizeof(g)); prefs.end();
    g_last_save = millis(); g_dirty = false;
}
void stats_reset() { uint32_t bc = g.boot_count; memset(&g, 0, sizeof(g)); g.boot_count = bc; g_seen_n = 0; g_seen_head = 0; g_dirty = true; g_last_save = 0; stats_save_if_due(); }
