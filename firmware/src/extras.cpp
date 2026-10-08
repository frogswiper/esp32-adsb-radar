#include "extras.h"
#include "net_util.h"
#include "airports.h"
#include "adsb.h"
#include <Arduino.h>

static IssState   g_iss = {};
static MetarState g_metar = {};
static PsramBuffer g_buf;

bool extras_fetch_iss(IssState& out) {
    int rc = http_get_to_buffer("https://api.wheretheiss.at/v1/satellites/25544", g_buf, 8000);
    if (rc != 200) return false;
    JsonDocument doc(&g_psram_alloc);
    if (deserializeJson(doc, g_buf.data(), g_buf.size())) return false;
    out.lat = doc["latitude"] | 0.0f; out.lon = doc["longitude"] | 0.0f;
    out.alt_km = doc["altitude"] | 0.0f; out.vel_kmh = doc["velocity"] | 0.0f;
    out.valid = true; out.fetched_ms = millis();
    return true;
}

bool extras_fetch_metar(const char* icao, MetarState& out) {
    char url[128]; snprintf(url, sizeof(url), "https://aviationweather.gov/api/data/metar?ids=%s&format=raw", icao);
    int rc = http_get_to_buffer(url, g_buf, 10000);
    if (rc != 200 || g_buf.size() < 10) { Serial.printf("[METAR] http %d, %u bytes\n", rc, (unsigned)g_buf.size()); return false; }
    strlcpy(out.station, icao, sizeof(out.station));
    const char* raw = g_buf.data(); if (!strncmp(raw, "METAR ", 6)) raw += 6;
    strlcpy(out.raw, raw, sizeof(out.raw));
    for (char* p = out.raw; *p; p++) if (*p == '\n' || *p == '\r') { *p = 0; break; }
    out.valid = true; out.fetched_ms = millis();
    return true;
}

const IssState&   extras_iss()   { return g_iss; }
const MetarState& extras_metar() { return g_metar; }
void extras_set_iss(const IssState& s)     { g_iss = s; }
void extras_set_metar(const MetarState& m) { g_metar = m; }

const char* extras_metar_station(float lat, float lon) {
    // prefer fields with an IATA code (scheduled traffic → METAR available), nearest first
    static char best[6]; best[0] = 0; float best_d = 200.0f;
    for (size_t i = 0; i < AIRPORT_COUNT; i++) {
        if (!AIRPORTS[i].iata[0]) continue;
        float d = geo_distance_km(lat, lon, AIRPORTS[i].lat, AIRPORTS[i].lon);
        if (d < best_d) { best_d = d; strlcpy(best, AIRPORTS[i].icao, sizeof(best)); }
    }
    return best;
}
