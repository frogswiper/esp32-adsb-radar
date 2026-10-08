#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MAX_AIRCRAFT 160
#define TRAIL_LEN    20

enum AcClass : uint8_t { CLS_AIRLINER = 0, CLS_LIGHT = 1, CLS_HELI = 2, CLS_MILITARY = 3, CLS_OTHER = 4 };

struct Aircraft {
    char     hex[8];
    char     flight[10];    // callsign, trimmed
    char     reg[12];       // registration
    char     type[6];       // ICAO type code
    float    lat, lon;
    int32_t  alt_ft;        // barometric altitude, 0 when on ground
    bool     on_ground;
    int16_t  gs_kt;         // ground speed
    int16_t  track;         // 0..359, -1 unknown
    int16_t  vr_fpm;        // vertical rate
    char     squawk[5];
    bool     emergency;     // squawk 7500/7600/7700 or emergency != none
    bool     military;      // dbFlags bit 0
    char     category[3];   // "A3" etc.
    char     route[20];     // "OSL-SOG-SDN" (IATA codes) from the route database, empty if unknown
    char     dest_city[14]; // destination city name (ASCII)
    char     org_city[14];
    char     org_name[26];  // airport names (ASCII, truncated)
    char     dest_name[26];
    bool     route_tried;
    // closest point of approach to the radar centre (straight-line extrapolation)
    float    cpa_km;
    float    cpa_min;       // minutes until CPA (0 = already there / receding)
    bool     alert;         // overhead alert active
    uint8_t  cls;           // AcClass
    bool     watch;         // matches the watchlist
    bool     notable;       // notable heavy / special type
    float    elev_deg;      // elevation angle above horizon from the radar centre
    float    org_lat, org_lon, dst_lat, dst_lon;   // route airports (0 = unknown)
    float    dist_km;
    float    bearing;       // 0..360 from centre
    // trail (ring buffer of past positions)
    float    trail_lat[TRAIL_LEN];
    float    trail_lon[TRAIL_LEN];
    int16_t  trail_alt[TRAIL_LEN];   // altitude / 10 ft
    uint8_t  trail_n, trail_head;
    uint8_t  missing;       // consecutive fetches without this aircraft
    uint32_t first_seen_ms;
};

struct AdsbStats {
    int       count;         // aircraft currently tracked (after filters)
    int       nearest;       // index or -1
    int       highest;       // index or -1
    int       fastest;       // index or -1
    int       emergencies;   // count
    int       alerts;        // overhead alerts active
    int       watch_count;   // watchlist / notable aircraft in range
    char      last_source_note[24];
    uint32_t  last_ok_ms;    // millis() of last successful fetch
    uint32_t  last_latency_ms;
    int       last_http;     // last HTTP status / error
    int       fail_streak;
    uint32_t  total_fetches, total_failures;
    char      source[16];    // "adsb.lol" / "adsb.fi"
};

// Call once (allocates PSRAM).
void adsb_init();

// Fetch + merge. Called from the network task. Returns true on success.
bool adsb_fetch(float lat, float lon, int range_km);

// ── Data access (callers must hold adsb_lock()) ──────────────────────────────
void             adsb_lock();
void             adsb_unlock();
Aircraft*        adsb_list();         // MAX_AIRCRAFT slots; valid entries have hex[0] != 0
int              adsb_count();        // number of valid slots scanned up to
const AdsbStats& adsb_stats();
int              adsb_find_hex(const char* hex);
void             adsb_clear();        // drop everything (location changed)
void             adsb_recompute(float lat, float lon);   // distances/bearings/stats

// Resolve routes for callsigns not yet looked up (adsb.im routeset). Call after adsb_fetch().
void adsb_fetch_routes();
// Airline short name from a callsign's 3-letter ICAO prefix ("" if unknown).
const char* adsb_airline_name(const char* callsign);
const char* adsb_type_name(const char* icao_type);      // "" if unknown
const char* adsb_squawk_meaning(const char* squawk);    // "" if nothing special
const char* adsb_category_name(const char* category);   // "" if unknown
const char* adsb_class_name(uint8_t cls);
bool        adsb_visible(const Aircraft& a);            // passes hide-ground + class filters
const char* adsb_compass16(float bearing);

// ── Geometry helpers ─────────────────────────────────────────────────────────
float geo_distance_km(float lat1, float lon1, float lat2, float lon2);
float geo_bearing_deg(float lat1, float lon1, float lat2, float lon2);
