#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_CITIES 80
#define CITY_NAME_LEN 32

struct CityList {
    int  count;
    char names[MAX_CITIES][CITY_NAME_LEN];
};

struct LocalInfo {          // from Open-Meteo forecast (timezone + current weather)
    bool  valid;
    int   utc_offset_seconds;
    float temp_c;
    float wind_mps;
    float gust_mps;
    int   wind_dir;        // degrees from
    int   wmo_code;
    char  tz_name[32];
};

// Public-IP geolocation (ip-api.com, fallback ipwho.is). label like "Oslo, NO".
bool geo_locate_by_ip(float& lat, float& lon, char* label, size_t label_len);

// Top cities by population for a country (countriesnow.space); fallback alphabetical list.
bool geo_fetch_cities(const char* country_name, CityList& out);

// City name + ISO-2 → coordinates via Open-Meteo geocoding (country-filtered).
bool geo_geocode(const char* city, const char* cc, float& lat, float& lon, char* label, size_t label_len);

// Timezone offset + current temperature for a position.
bool geo_fetch_local_info(float lat, float lon, LocalInfo& out);

// WMO weather code → short text
const char* geo_wmo_short(int code);
