#pragma once
#include <stdint.h>
#include <stdbool.h>

enum LocMode : uint8_t { LOC_AUTO_IP = 0, LOC_CITY = 1, LOC_MANUAL = 2 };
enum Units   : uint8_t { UNITS_METRIC = 0, UNITS_AVIATION = 1 };
enum Source  : uint8_t { SRC_AUTO = 0, SRC_ADSB_LOL = 1, SRC_ADSB_FI = 2 };

struct Settings {
    char     wifi_ssid[64];
    char     wifi_password[64];

    uint8_t  loc_mode;          // LocMode
    char     place[48];         // human-readable location label ("Oslo, NO")
    char     country_cc[4];     // ISO-2 of chosen country (city mode)
    uint8_t  continent;         // index into CONTINENT_NAMES
    char     city[48];          // chosen / typed city
    float    lat, lon;          // effective radar centre
    float    man_lat, man_lon;  // manual override fields

    uint16_t range_km;          // ring radius (outer)
    uint16_t update_s;          // aircraft refresh period
    uint8_t  units;             // Units
    uint8_t  source;            // Source
    bool     sweep, trails, labels, hide_ground, auto_range, night_dim, highlight_mil;
    uint8_t  brightness;        // 10..255
    uint8_t  alert_km;          // overhead alert radius, 0 = off
    bool     alert_bright;      // full backlight while an alert is active
    bool     map_underlay;      // coastline / lakes / borders behind the rings
    bool     airports;          // airport markers
    // integrations / extras
    char     watchlist[96];     // comma-separated registration / callsign prefixes
    char     ntfy_topic[48];    // ntfy.sh topic, empty = off
    char     webhook_url[96];   // POST JSON on alerts, empty = off
    char     mqtt_uri[96];      // mqtt://user:pass@host:port, empty = off
    char     local_url[96];     // dump1090 / readsb aircraft.json on the LAN, empty = off
    char     panel_pass[32];    // web panel password (Basic Auth, user "admin"), empty = open
    uint8_t  class_mask;        // bit0 airliner, bit1 light, bit2 helicopter, bit3 military, bit4 other (1 = show)
    bool     iss, metar;        // extras
    bool     notify_emergency, notify_watch, notify_alert;
    int      utc_offset_seconds;
};

Settings& settings_get();
void      settings_load();
void      settings_save();
void      settings_factory_reset();   // wipe namespace (WiFi kept) and reload defaults
bool      settings_has_wifi();
bool      settings_has_location();

// range steps shared by UI + auto-range
extern const uint16_t RANGE_STEPS_KM[];
extern const int      RANGE_STEP_COUNT;
int  settings_range_index();               // index of current range in RANGE_STEPS_KM
void settings_set_range_index(int idx);
