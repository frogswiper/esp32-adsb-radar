#include "adsb.h"
#include "net_util.h"
#include "settings.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <math.h>
#include "airlines.h"
#include "actypes.h"
#include "notify.h"
#include "stats.h"

static Aircraft*         g_ac    = nullptr;   // persistent tracked list (PSRAM)
static int               g_n     = 0;         // highest used slot + 1
static AdsbStats         g_stats = {};
static SemaphoreHandle_t g_mutex = nullptr;
static PsramBuffer       g_body;

// scratch parsed list (PSRAM)
struct Parsed { char hex[8]; Aircraft a; };
static Aircraft* g_tmp = nullptr;

void adsb_init() {
    g_ac  = (Aircraft*)heap_caps_calloc(MAX_AIRCRAFT, sizeof(Aircraft), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_tmp = (Aircraft*)heap_caps_calloc(MAX_AIRCRAFT, sizeof(Aircraft), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_mutex = xSemaphoreCreateMutex();
    g_stats.nearest = g_stats.highest = g_stats.fastest = -1;
}

void adsb_lock()   { xSemaphoreTake(g_mutex, portMAX_DELAY); }
void adsb_unlock() { xSemaphoreGive(g_mutex); }
Aircraft* adsb_list() { return g_ac; }
int adsb_count()      { return g_n; }
const AdsbStats& adsb_stats() { return g_stats; }

int adsb_find_hex(const char* hex) {
    for (int i = 0; i < g_n; i++)
        if (g_ac[i].hex[0] && strcmp(g_ac[i].hex, hex) == 0) return i;
    return -1;
}

float geo_distance_km(float lat1, float lon1, float lat2, float lon2) {
    const float R = 6371.0f;
    float dlat = radians(lat2 - lat1), dlon = radians(lon2 - lon1);
    float a = sinf(dlat / 2) * sinf(dlat / 2) +
              cosf(radians(lat1)) * cosf(radians(lat2)) * sinf(dlon / 2) * sinf(dlon / 2);
    return R * 2 * atan2f(sqrtf(a), sqrtf(1 - a));
}
float geo_bearing_deg(float lat1, float lon1, float lat2, float lon2) {
    float p1 = radians(lat1), p2 = radians(lat2), dl = radians(lon2 - lon1);
    float y = sinf(dl) * cosf(p2);
    float x = cosf(p1) * sinf(p2) - sinf(p1) * cosf(p2) * cosf(dl);
    float b = degrees(atan2f(y, x));
    if (b < 0) b += 360.0f;
    return b;
}

void adsb_recompute(float lat, float lon) {
    const Settings& s = settings_get();
    g_stats.count = 0; g_stats.emergencies = 0; g_stats.alerts = 0; g_stats.watch_count = 0;
    g_stats.nearest = g_stats.highest = g_stats.fastest = -1;
    float best_d = 1e9f; int best_alt = -1, best_gs = -1;
    for (int i = 0; i < g_n; i++) {
        Aircraft& a = g_ac[i];
        if (!a.hex[0]) continue;
        a.dist_km = geo_distance_km(lat, lon, a.lat, a.lon);
        a.bearing = geo_bearing_deg(lat, lon, a.lat, a.lon);
        // closest point of approach: position p (km, east/north), velocity v (km/min)
        a.cpa_km = a.dist_km; a.cpa_min = 0; a.alert = false;
        if (!a.on_ground && a.track >= 0 && a.gs_kt > 30) {
            float br = radians(a.bearing), tr = radians((float)a.track);
            float px = a.dist_km * sinf(br), py = a.dist_km * cosf(br);
            float spd = a.gs_kt * 1.852f / 60.0f;
            float vx = spd * sinf(tr), vy = spd * cosf(tr);
            float vv = vx * vx + vy * vy;
            float t = vv > 0 ? -(px * vx + py * vy) / vv : 0;
            if (t < 0) t = 0;
            a.cpa_km = sqrtf((px + vx * t) * (px + vx * t) + (py + vy * t) * (py + vy * t));
            a.cpa_min = t;
            if (s.alert_km > 0 && a.cpa_min <= 15.0f && a.cpa_km <= (float)s.alert_km) a.alert = true;
        } else if (s.alert_km > 0 && !a.on_ground && a.dist_km <= (float)s.alert_km) a.alert = true;
        // elevation angle (for the spotter line)
        {
            float alt_km = a.on_ground ? 0 : a.alt_ft * 0.0003048f;
            a.elev_deg = a.dist_km > 0.05f ? degrees(atan2f(alt_km, a.dist_km)) : 90.0f;
        }
        if (!adsb_visible(a)) continue;
        g_stats.count++;
        if (a.emergency) g_stats.emergencies++;
        if (a.alert) g_stats.alerts++;
        if (a.watch || a.notable) g_stats.watch_count++;
        if (a.dist_km < best_d)  { best_d = a.dist_km;  g_stats.nearest = i; }
        if (a.alt_ft > best_alt) { best_alt = a.alt_ft; g_stats.highest = i; }
        if (a.gs_kt  > best_gs)  { best_gs = a.gs_kt;   g_stats.fastest = i; }
    }
}

static void trim_copy(char* dst, size_t n, const char* src) {
    if (!src) { dst[0] = 0; return; }
    while (*src == ' ') src++;
    strlcpy(dst, src, n);
    size_t l = strlen(dst);
    while (l && dst[l - 1] == ' ') dst[--l] = 0;
}

// Parse one API reply (readsb "ac"/"aircraft" array) into g_tmp. Returns count.
static int parse_reply(const char* body, size_t len) {
    JsonDocument filter(&g_psram_alloc);
    const char* keys[] = {"hex","flight","r","t","alt_baro","gs","track","lat","lon",
                          "baro_rate","geom_rate","squawk","emergency","dbFlags","category"};
    for (const char* k : keys) { filter["ac"][0][k] = true; filter["aircraft"][0][k] = true; }

    JsonDocument doc(&g_psram_alloc);
    DeserializationError err = deserializeJson(doc, body, len, DeserializationOption::Filter(filter));
    if (err) { Serial.printf("[ADSB] json error: %s\n", err.c_str()); return -1; }

    JsonArray arr = doc["ac"].as<JsonArray>();
    if (arr.isNull()) arr = doc["aircraft"].as<JsonArray>();
    if (arr.isNull()) return -1;

    int n = 0;
    for (JsonObject o : arr) {
        if (n >= MAX_AIRCRAFT) break;
        if (!o["lat"].is<float>() || !o["lon"].is<float>()) continue;   // no position → skip
        const char* hex = o["hex"] | "";
        if (!hex[0]) continue;
        Aircraft& a = g_tmp[n];
        memset(&a, 0, sizeof(a));
        strlcpy(a.hex, hex, sizeof(a.hex));
        trim_copy(a.flight, sizeof(a.flight), o["flight"] | "");
        trim_copy(a.reg,    sizeof(a.reg),    o["r"] | "");
        trim_copy(a.type,   sizeof(a.type),   o["t"] | "");
        strlcpy(a.category, o["category"] | "", sizeof(a.category));
        a.lat = o["lat"].as<float>();
        a.lon = o["lon"].as<float>();
        if (o["alt_baro"].is<const char*>()) { a.on_ground = true; a.alt_ft = 0; }
        else a.alt_ft = o["alt_baro"] | 0;
        a.gs_kt  = (int16_t)(o["gs"] | 0.0f);
        a.track  = o["track"].is<float>() ? (int16_t)lroundf(o["track"].as<float>()) % 360 : -1;
        a.vr_fpm = o["baro_rate"].is<int>() ? (int16_t)(o["baro_rate"] | 0) : (int16_t)(o["geom_rate"] | 0);
        strlcpy(a.squawk, o["squawk"] | "", sizeof(a.squawk));
        const char* em = o["emergency"] | "none";
        a.emergency = (strcmp(em, "none") != 0 && em[0]) ||
                      !strcmp(a.squawk, "7500") || !strcmp(a.squawk, "7600") || !strcmp(a.squawk, "7700");
        a.military = (o["dbFlags"] | 0) & 1;
        n++;
    }
    return n;
}

// ── classification ──────────────────────────────────────────────────────────
static bool in_list(const char* v, const char* const* list) { for (; *list; list++) if (!strcmp(v, *list)) return true; return false; }
static const char* const HELI_TYPES[]  = {"EC35","EC45","EC75","EC55","EC30","EC20","A109","A139","A169","A189","S92","S76","B06","B407","B412","B429","R44","R66","AS50","AS55","AS65","H160","H60","UH1","NH90","AW09","EH10","LYNX","PUMA","S61", nullptr};
static const char* const NOTABLE_TYPES[] = {"A388","A124","A225","C17","C5M","C5","B748","B744","A3ST","A337","B52","E3CF","E3TF","VC25","B742","KC13","KC10","A400","C130","C30J","P8","E6","RC13","GLF6","B763", nullptr};
static uint8_t classify(const Aircraft& a) {
    if (a.military) return CLS_MILITARY;
    if (a.category[0] == 'A' && a.category[1] == '7') return CLS_HELI;
    if (in_list(a.type, HELI_TYPES)) return CLS_HELI;
    if (a.category[0] == 'B' || a.category[0] == 'C') return CLS_OTHER;
    if (a.category[0] == 'A' && (a.category[1] >= '3' && a.category[1] <= '5')) return CLS_AIRLINER;
    if (a.category[0] == 'A' && (a.category[1] == '1')) return CLS_LIGHT;
    if (a.type[0]) {
        const char* n = adsb_type_name(a.type);
        if (strstr(n, "Boeing") || strstr(n, "Airbus") || strstr(n, "Embraer") || strstr(n, "ATR") || strstr(n, "Dash") || strstr(n, "CRJ") || strstr(n, "Fokker") || strstr(n, "Saab")) return CLS_AIRLINER;
        if (strstr(n, "Cessna 1") || strstr(n, "Piper") || strstr(n, "Diamond") || strstr(n, "Cirrus") || strstr(n, "Beechcraft Bon") || strstr(n, "Robinson")) return CLS_LIGHT;
        if (adsb_airline_name(a.flight)[0]) return CLS_AIRLINER;
        return a.category[0] == 'A' && a.category[1] == '2' ? CLS_LIGHT : CLS_OTHER;
    }
    if (adsb_airline_name(a.flight)[0]) return CLS_AIRLINER;
    return CLS_OTHER;
}
static bool watch_match(const Aircraft& a) {
    const Settings& s = settings_get();
    if (!s.watchlist[0]) return false;
    char list[96]; strlcpy(list, s.watchlist, sizeof(list));
    for (char* tok = strtok(list, ", "); tok; tok = strtok(nullptr, ", ")) {
        size_t l = strlen(tok); if (!l) continue;
        if (!strncasecmp(a.flight, tok, l) || !strncasecmp(a.reg, tok, l) || !strcasecmp(a.type, tok) || !strcasecmp(a.hex, tok)) return true;
    }
    return false;
}
static void annotate(Aircraft& a) {
    a.cls = classify(a);
    a.notable = in_list(a.type, NOTABLE_TYPES);
    a.watch = watch_match(a);
}

const char* adsb_class_name(uint8_t c) {
    switch (c) { case CLS_AIRLINER: return "airliner"; case CLS_LIGHT: return "light aircraft"; case CLS_HELI: return "helicopter"; case CLS_MILITARY: return "military"; default: return "other"; }
}
bool adsb_visible(const Aircraft& a) {
    const Settings& s = settings_get();
    if (s.hide_ground && a.on_ground) return false;
    if (!(s.class_mask & (1 << (a.cls & 7)))) return false;
    return true;
}
const char* adsb_compass16(float b) {
    static const char* n[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"};
    return n[((int)lroundf(b / 22.5f)) & 15];
}

// Merge g_tmp[0..n) into persistent list, keeping trails for known aircraft.
static void merge(int n, uint32_t now) {
    const Settings& sc = settings_get();
    for (int k = 0; k < n; k++) g_tmp[k].dist_km = geo_distance_km(sc.lat, sc.lon, g_tmp[k].lat, g_tmp[k].lon);
    // mark all as missing
    for (int i = 0; i < g_n; i++) if (g_ac[i].hex[0]) g_ac[i].missing++;

    for (int k = 0; k < n; k++) {
        Aircraft& t = g_tmp[k];
        int idx = adsb_find_hex(t.hex);
        if (idx < 0) {
            // find free slot
            for (int i = 0; i < MAX_AIRCRAFT; i++) if (!g_ac[i].hex[0]) { idx = i; break; }
            if (idx < 0) continue;
            if (idx >= g_n) g_n = idx + 1;
            memcpy(&g_ac[idx], &t, sizeof(Aircraft));
            g_ac[idx].first_seen_ms = now;
            g_ac[idx].trail_n = 0; g_ac[idx].trail_head = 0;
            annotate(g_ac[idx]);
            {
                Aircraft& na = g_ac[idx];
                char title[40], msg[96];
                const char* who = na.flight[0] ? na.flight : (na.reg[0] ? na.reg : na.hex);
                if (na.emergency) {
                    snprintf(title, sizeof(title), "EMERGENCY %s", who);
                    snprintf(msg, sizeof(msg), "squawk %s, %s %s, %.0f ft, %.1f km away", na.squawk, na.type, na.reg, (float)na.alt_ft, na.dist_km);
                    if (notify_event(NK_EMERGENCY, na.hex, title, msg)) stats_note_emergency();
                } else if (na.watch || na.notable) {
                    snprintf(title, sizeof(title), "%s %s in range", na.watch ? "Watchlist" : "Notable", who);
                    snprintf(msg, sizeof(msg), "%s %s (%s), %.0f ft, %.1f km away", na.type, na.reg, adsb_type_name(na.type), (float)na.alt_ft, na.dist_km);
                    notify_event(NK_WATCHLIST, na.hex, title, msg);
                }
            }
        } else {
            Aircraft& a = g_ac[idx];
            // push previous position to trail if moved
            if (a.trail_n == 0 || fabsf(a.lat - t.lat) > 1e-4f || fabsf(a.lon - t.lon) > 1e-4f) {
                a.trail_lat[a.trail_head] = a.lat;
                a.trail_lon[a.trail_head] = a.lon;
                a.trail_alt[a.trail_head] = (int16_t)(a.alt_ft / 10);
                a.trail_head = (a.trail_head + 1) % TRAIL_LEN;
                if (a.trail_n < TRAIL_LEN) a.trail_n++;
            }
            // copy live fields
            strlcpy(a.flight, t.flight, sizeof(a.flight));
            if (t.reg[0])  strlcpy(a.reg,  t.reg,  sizeof(a.reg));
            if (t.type[0]) strlcpy(a.type, t.type, sizeof(a.type));
            strlcpy(a.category, t.category, sizeof(a.category));
            a.lat = t.lat; a.lon = t.lon; a.alt_ft = t.alt_ft; a.on_ground = t.on_ground;
            a.gs_kt = t.gs_kt; a.track = t.track; a.vr_fpm = t.vr_fpm;
            strlcpy(a.squawk, t.squawk, sizeof(a.squawk));
            bool was_emg = a.emergency;
            a.emergency = t.emergency; a.military = t.military;
            if ((t.type[0] && strcmp(a.type, t.type)) || a.cls == 0) a.cls = classify(a);
            if (a.emergency && !was_emg) {
                char title[40], msg[96];
                const char* who = a.flight[0] ? a.flight : (a.reg[0] ? a.reg : a.hex);
                snprintf(title, sizeof(title), "EMERGENCY %s", who);
                snprintf(msg, sizeof(msg), "squawk %s, %s %s, %.0f ft, %.1f km away", a.squawk, a.type, a.reg, (float)a.alt_ft, a.dist_km);
                if (notify_event(NK_EMERGENCY, a.hex, title, msg)) stats_note_emergency();
            }
        }
        g_ac[idx].missing = 0;
    }
    // drop aircraft not seen for 3 consecutive fetches
    for (int i = 0; i < g_n; i++)
        if (g_ac[i].hex[0] && g_ac[i].missing >= 3) g_ac[i].hex[0] = 0;
    while (g_n > 0 && !g_ac[g_n - 1].hex[0]) g_n--;
}

static const char* build_url(char* url, size_t n, int which, float lat, float lon, int range_km) {
    int nm = (int)ceilf(range_km / 1.852f);
    if (nm > 250) nm = 250;
    if (which == SRC_ADSB_FI)
        snprintf(url, n, "https://opendata.adsb.fi/api/v2/lat/%.4f/lon/%.4f/dist/%d", lat, lon, nm);
    else
        snprintf(url, n, "https://api.adsb.lol/v2/lat/%.4f/lon/%.4f/dist/%d", lat, lon, nm);
    return which == SRC_ADSB_FI ? "adsb.fi" : "adsb.lol";
}

static uint32_t g_cooldown_until[3] = {0, 0, 0};   // indexed by Source
static int      g_rr = 0;

bool adsb_fetch(float lat, float lon, int range_km) {
    const Settings& s = settings_get();
    uint32_t now = millis();
    int order[2]; int tries;
    if (s.source == SRC_ADSB_FI)       { order[0] = SRC_ADSB_FI;  order[1] = SRC_ADSB_LOL; tries = 1; }
    else if (s.source == SRC_ADSB_LOL) { order[0] = SRC_ADSB_LOL; order[1] = SRC_ADSB_FI;  tries = 1; }
    else {
        // auto: round-robin between sources (halves the per-source request rate),
        // skipping a source that is cooling down after a 429 / failure.
        int first = (g_rr++ & 1) ? SRC_ADSB_FI : SRC_ADSB_LOL;
        int second = (first == SRC_ADSB_FI) ? SRC_ADSB_LOL : SRC_ADSB_FI;
        if ((int32_t)(g_cooldown_until[first] - now) > 0 && (int32_t)(g_cooldown_until[second] - now) <= 0) { int t = first; first = second; second = t; }
        order[0] = first; order[1] = second; tries = 2;
    }

    // own dump1090 / readsb receiver on the LAN first (same JSON layout as the aggregators)
    if (s.local_url[0]) {
        uint32_t t0 = millis();
        int rc = http_get_to_buffer(s.local_url, g_body, 5000);
        uint32_t dt = millis() - t0;
        g_stats.total_fetches++; g_stats.last_http = rc;
        int n = (rc == 200) ? parse_reply(g_body.data(), g_body.size()) : -1;
        if (n >= 0) {
            adsb_lock();
            merge(n, millis()); adsb_recompute(lat, lon);
            g_stats.last_ok_ms = millis(); g_stats.last_latency_ms = dt; g_stats.fail_streak = 0;
            strlcpy(g_stats.source, "local rx", sizeof(g_stats.source));
            adsb_unlock();
            Serial.printf("[ADSB] local receiver: %d aircraft, %lu ms\n", n, (unsigned long)dt);
            return true;
        }
        g_stats.total_failures++;
        Serial.printf("[ADSB] local receiver failed (%d), falling back to internet\n", rc);
    }

    for (int t = 0; t < tries; t++) {
        if (tries == 2 && t == 0 && (int32_t)(g_cooldown_until[order[0]] - now) > 0 && (int32_t)(g_cooldown_until[order[1]] - now) > 0) {
            // both cooling down: wait for the one that frees up first
        }
        char url[160];
        const char* name = build_url(url, sizeof(url), order[t], lat, lon, range_km);
        uint32_t t0 = millis();
        int rc = http_get_to_buffer(url, g_body, 12000);
        uint32_t dt = millis() - t0;
        g_stats.total_fetches++;
        g_stats.last_http = rc;
        if (rc != 200) {
            Serial.printf("[ADSB] %s http %d (%lu ms)\n", name, rc, (unsigned long)dt);
            g_stats.total_failures++; g_stats.fail_streak++;
            g_cooldown_until[order[t]] = millis() + (rc == 429 ? 30000UL : 10000UL);
            continue;
        }
        int n = parse_reply(g_body.data(), g_body.size());
        if (n < 0) { g_stats.total_failures++; g_stats.fail_streak++; continue; }

        adsb_lock();
        merge(n, millis());
        adsb_recompute(lat, lon);
        g_stats.last_ok_ms = millis();
        g_stats.last_latency_ms = dt;
        g_stats.fail_streak = 0;
        strlcpy(g_stats.source, name, sizeof(g_stats.source));
        adsb_unlock();
        Serial.printf("[ADSB] %s: %d aircraft, %u bytes, %lu ms, heap %u KB\n", name, n, (unsigned)g_body.size(), (unsigned long)dt,
                      (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
        return true;
    }
    return false;
}

void adsb_clear() {
    adsb_lock();
    memset(g_ac, 0, sizeof(Aircraft) * MAX_AIRCRAFT);
    g_n = 0;
    g_stats.count = 0; g_stats.emergencies = 0;
    g_stats.nearest = g_stats.highest = g_stats.fastest = -1;
    adsb_unlock();
}

// ── Route cache (callsign → IATA route + destination city) ──────────────────
#define ROUTE_CACHE 256
struct RouteEntry { char callsign[10]; char route[20]; char city[14]; char org_city[14]; char org_name[26]; char dest_name[26]; float org_lat, org_lon, dst_lat, dst_lon; bool known; };
static RouteEntry* g_routes = nullptr;
static int g_route_n = 0, g_route_next = 0;
static PsramBuffer g_rbody;

static void ascii_fold_copy(char* dst, size_t n, const char* src) {
    size_t o = 0;
    for (const unsigned char* p = (const unsigned char*)src; *p && o + 1 < n; p++) {
        if (*p < 0x80) { dst[o++] = *p; continue; }
        if (*p == 0xC3 && p[1]) {
            unsigned char c = p[1]; p++;
            const char* r = "?";
            if (c==0x85||c==0xA5||c==0x84||c==0xA4||c==0x80||c==0xA0||c==0x81||c==0xA1||c==0x83||c==0xA3) r = "a";
            else if (c==0x86||c==0xA6) r = "ae";
            else if (c==0x98||c==0xB8||c==0x96||c==0xB6||c==0x93||c==0xB3||c==0x92||c==0xB2) r = "o";
            else if (c==0x9C||c==0xBC||c==0x9A||c==0xBA) r = "u";
            else if (c==0x89||c==0xA9||c==0x88||c==0xA8||c==0x8A||c==0xAA) r = "e";
            else if (c==0x8D||c==0xAD) r = "i";
            else if (c==0x87||c==0xA7) r = "c";
            else if (c==0x91||c==0xB1) r = "n";
            if (c < 0xA0 && r[0] >= 'a') { char u[3] = {(char)(r[0]-32), r[1], 0}; r = u; while (*r && o + 1 < n) dst[o++] = *r++; continue; }
            while (*r && o + 1 < n) dst[o++] = *r++;
            continue;
        }
        if (*p == 0xC5 && p[1]) {
            unsigned char c = p[1]; p++;
            char r = (c==0x81||c==0x82)?'l':(c==0x83||c==0x84||c==0x87||c==0x88)?'n':(c==0x9A||c==0x9B||c==0x9E||c==0x9F)?'s':(c==0xB9||c==0xBA||c==0xBB||c==0xBC||c==0xBD||c==0xBE)?'z':(c==0x98||c==0x99)?'r':(c==0x8C||c==0x8D)?'c':(c==0x92||c==0x93)?'o':(c==0xA4||c==0xA5)?'t':'?';
            dst[o++] = (c & 1) == 0 && c < 0xA0 && r != '?' ? (char)toupper(r) : r;   // even code points are the capitals in this block
            continue;
        }
        while ((p[1] & 0xC0) == 0x80) p++;
    }
    dst[o] = 0;
}

static RouteEntry* route_find(const char* cs) {
    for (int i = 0; i < g_route_n; i++) if (!strcmp(g_routes[i].callsign, cs)) return &g_routes[i];
    return nullptr;
}
static RouteEntry* route_add(const char* cs) {
    if (!g_routes) return nullptr;
    RouteEntry* e;
    if (g_route_n < ROUTE_CACHE) e = &g_routes[g_route_n++];
    else { e = &g_routes[g_route_next]; g_route_next = (g_route_next + 1) % ROUTE_CACHE; }
    memset(e, 0, sizeof(*e));
    strlcpy(e->callsign, cs, sizeof(e->callsign));
    return e;
}

void adsb_fetch_routes() {
    if (!g_routes) g_routes = (RouteEntry*)heap_caps_calloc(ROUTE_CACHE, sizeof(RouteEntry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // collect up to 20 callsigns that need a lookup
    char want[20][10]; float wlat[20], wlon[20]; int wn = 0;
    adsb_lock();
    for (int i = 0; i < g_n && wn < 20; i++) {
        Aircraft& a = g_ac[i];
        if (!a.hex[0] || !a.flight[0] || a.route_tried) continue;
        RouteEntry* e = route_find(a.flight);
        if (e) {   // cached
            strlcpy(a.route, e->route, sizeof(a.route)); strlcpy(a.dest_city, e->city, sizeof(a.dest_city));
            strlcpy(a.org_city, e->org_city, sizeof(a.org_city)); strlcpy(a.org_name, e->org_name, sizeof(a.org_name)); strlcpy(a.dest_name, e->dest_name, sizeof(a.dest_name));
            a.org_lat = e->org_lat; a.org_lon = e->org_lon; a.dst_lat = e->dst_lat; a.dst_lon = e->dst_lon;
            a.route_tried = true; continue;
        }
        bool dup = false; for (int k = 0; k < wn; k++) if (!strcmp(want[k], a.flight)) dup = true;
        if (dup) continue;
        strlcpy(want[wn], a.flight, 10); wlat[wn] = a.lat; wlon[wn] = a.lon; wn++;
    }
    adsb_unlock();
    if (wn == 0) return;

    char body[20 * 60 + 32]; size_t o = 0;
    o += snprintf(body + o, sizeof(body) - o, "{\"planes\":[");
    for (int k = 0; k < wn; k++)
        o += snprintf(body + o, sizeof(body) - o, "%s{\"callsign\":\"%s\",\"lat\":%.3f,\"lon\":%.3f}", k ? "," : "", want[k], wlat[k], wlon[k]);
    snprintf(body + o, sizeof(body) - o, "]}");

    int rc = http_post_json_to_buffer("https://adsb.im/api/0/routeset", body, g_rbody, 12000);
    if (rc != 200) { Serial.printf("[ROUTE] http %d\n", rc); return; }

    JsonDocument filter(&g_psram_alloc);
    filter[0]["callsign"] = true; filter[0]["_airport_codes_iata"] = true; filter[0]["_airports"][0]["location"] = true; filter[0]["_airports"][0]["name"] = true;
    filter[0]["_airports"][0]["lat"] = true; filter[0]["_airports"][0]["lon"] = true;
    JsonDocument doc(&g_psram_alloc);
    if (deserializeJson(doc, g_rbody.data(), g_rbody.size(), DeserializationOption::Filter(filter))) { Serial.println("[ROUTE] bad json"); return; }

    adsb_lock();
    int found = 0;
    for (int k = 0; k < wn; k++) {
        RouteEntry* e = route_add(want[k]);
        if (!e) break;
        for (JsonObject r : doc.as<JsonArray>()) {
            const char* cs = r["callsign"] | "";
            if (strcmp(cs, want[k])) continue;
            const char* codes = r["_airport_codes_iata"] | "";
            if (!codes[0] || !strcmp(codes, "unknown")) break;
            strlcpy(e->route, codes, sizeof(e->route));
            JsonArray ap = r["_airports"].as<JsonArray>();
            if (ap.size()) {
                ascii_fold_copy(e->city,      sizeof(e->city),      ap[ap.size() - 1]["location"] | "");
                ascii_fold_copy(e->dest_name, sizeof(e->dest_name), ap[ap.size() - 1]["name"] | "");
                ascii_fold_copy(e->org_city,  sizeof(e->org_city),  ap[0]["location"] | "");
                ascii_fold_copy(e->org_name,  sizeof(e->org_name),  ap[0]["name"] | "");
                e->org_lat = ap[0]["lat"] | 0.0f; e->org_lon = ap[0]["lon"] | 0.0f;
                e->dst_lat = ap[ap.size() - 1]["lat"] | 0.0f; e->dst_lon = ap[ap.size() - 1]["lon"] | 0.0f;
            }
            e->known = true; found++;
            break;
        }
        for (int i = 0; i < g_n; i++) {
            Aircraft& a = g_ac[i];
            if (a.hex[0] && !strcmp(a.flight, want[k])) {
                strlcpy(a.route, e->route, sizeof(a.route)); strlcpy(a.dest_city, e->city, sizeof(a.dest_city));
                strlcpy(a.org_city, e->org_city, sizeof(a.org_city)); strlcpy(a.org_name, e->org_name, sizeof(a.org_name)); strlcpy(a.dest_name, e->dest_name, sizeof(a.dest_name));
                a.org_lat = e->org_lat; a.org_lon = e->org_lon; a.dst_lat = e->dst_lat; a.dst_lon = e->dst_lon;
                a.route_tried = true;
            }
        }
    }
    adsb_unlock();
    Serial.printf("[ROUTE] looked up %d callsigns, %d routes known\n", wn, found);
}

const char* adsb_airline_name(const char* cs) {
    if (!cs || strlen(cs) < 4) return "";
    char code[4] = {cs[0], cs[1], cs[2], 0};
    for (int i = 0; i < 3; i++) if (code[i] < 'A' || code[i] > 'Z') return "";
    int lo = 0, hi = (int)AIRLINE_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = strcmp(AIRLINES[mid].icao, code);
        if (c == 0) return AIRLINES[mid].name;
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return "";
}

const char* adsb_type_name(const char* t) {
    if (!t || !t[0]) return "";
    int lo = 0, hi = (int)AC_TYPE_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = strcmp(AC_TYPES[mid].code, t);
        if (c == 0) return AC_TYPES[mid].name;
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return "";
}

const char* adsb_squawk_meaning(const char* sq) {
    if (!sq || !sq[0]) return "";
    if (!strcmp(sq, "7500")) return "HIJACK";
    if (!strcmp(sq, "7600")) return "radio failure";
    if (!strcmp(sq, "7700")) return "EMERGENCY";
    if (!strcmp(sq, "7000")) return "VFR conspicuity (Europe)";
    if (!strcmp(sq, "1200")) return "VFR (N. America)";
    if (!strcmp(sq, "2000")) return "no code assigned / entering SSR area";
    if (!strcmp(sq, "1000")) return "IFR, Mode S identified";
    if (!strcmp(sq, "7001")) return "military low level";
    if (!strcmp(sq, "7004")) return "aerobatics";
    if (!strcmp(sq, "7005")) return "high-energy manoeuvres";
    if (!strcmp(sq, "7010")) return "aerodrome traffic";
    if (!strcmp(sq, "7777")) return "military interceptor";
    if (!strcmp(sq, "0000")) return "unassigned / test";
    return "assigned by ATC";
}

const char* adsb_category_name(const char* c) {
    if (!c || strlen(c) < 2) return "";
    if (!strcmp(c, "A0")) return "no category info";
    if (!strcmp(c, "A1")) return "light (< 7 t)";
    if (!strcmp(c, "A2")) return "small (7-34 t)";
    if (!strcmp(c, "A3")) return "large (34-136 t)";
    if (!strcmp(c, "A4")) return "high-vortex large";
    if (!strcmp(c, "A5")) return "heavy (> 136 t)";
    if (!strcmp(c, "A6")) return "high performance";
    if (!strcmp(c, "A7")) return "rotorcraft";
    if (!strcmp(c, "B1")) return "glider / sailplane";
    if (!strcmp(c, "B2")) return "lighter-than-air";
    if (!strcmp(c, "B3")) return "parachutist";
    if (!strcmp(c, "B4")) return "ultralight";
    if (!strcmp(c, "B6")) return "unmanned";
    if (!strcmp(c, "B7")) return "space vehicle";
    if (!strcmp(c, "C1")) return "emergency vehicle";
    if (!strcmp(c, "C2")) return "service vehicle";
    if (!strcmp(c, "C3")) return "obstacle";
    return "";
}
