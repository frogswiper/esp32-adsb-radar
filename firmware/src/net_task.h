#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "geo.h"

// Commands executed on the network task (core 0). UI/main loop never block on HTTP.
enum NetCmd : uint8_t {
    NC_CONNECT_WIFI,     // (re)connect using saved credentials
    NC_LOCATE_IP,        // public-IP geolocation → settings lat/lon
    NC_FETCH_CITIES,     // arg1 = country name
    NC_GEOCODE,          // arg1 = city, arg2 = ISO-2 cc
    NC_APPLY_MANUAL,     // f1 = lat, f2 = lon
    NC_LOCAL_INFO,       // timezone + temperature for current position, then NTP
    NC_FETCH_AIRCRAFT,   // one ADS-B poll
    NC_NOTIFY,           // text = title, arg1 = message (kind in f1)
    NC_ISS,              // fetch ISS position
    NC_METAR,            // arg1 = ICAO station
};

struct NetMsg {
    NetCmd cmd;
    char   arg1[48];
    char   arg2[8];
    float  f1, f2;
    char   text[128];
    uint8_t kind;
};

// Event bits delivered back to the main loop
enum : uint32_t {
    EV_WIFI_OK       = 1 << 0,
    EV_WIFI_FAIL     = 1 << 1,
    EV_LOCATED       = 1 << 2,
    EV_LOCATE_FAIL   = 1 << 3,
    EV_CITIES        = 1 << 4,
    EV_CITIES_FAIL   = 1 << 5,
    EV_AIRCRAFT      = 1 << 6,
    EV_AIRCRAFT_FAIL = 1 << 7,
    EV_ISS           = 1 << 10,
    EV_METAR         = 1 << 11,
    EV_LOCAL_INFO    = 1 << 8,
    EV_STATUS        = 1 << 9,   // status text changed
};

void     net_task_start();
bool     net_send(NetCmd cmd, const char* arg1 = nullptr, const char* arg2 = nullptr, float f1 = 0, float f2 = 0);
bool     net_send_notify(const char* title, const char* message, uint8_t kind);
bool     net_busy();                       // a command is executing
bool     net_fetch_pending();              // an aircraft fetch is queued or running
uint32_t net_take_events();                // returns and clears pending event bits
void     net_status(char* buf, size_t n);  // latest human-readable status
bool     net_wifi_connected();
int      net_wifi_rssi();
void     net_wifi_ip(char* buf, size_t n);
const CityList&  net_cities();             // result of last NC_FETCH_CITIES
const LocalInfo& net_local_info();
