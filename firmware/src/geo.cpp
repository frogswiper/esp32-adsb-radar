#include "geo.h"
#include "net_util.h"
#include <ctype.h>

static PsramBuffer g_buf;

static void title_case(char* s) {
    bool start = true;
    for (char* p = s; *p; p++) {
        if (isalpha((unsigned char)*p)) { *p = start ? toupper((unsigned char)*p) : tolower((unsigned char)*p); start = false; }
        else start = (*p == ' ' || *p == '-' || *p == '(' || *p == '\'');
    }
}

bool geo_locate_by_ip(float& lat, float& lon, char* label, size_t label_len) {
    // 1) ip-api.com (plain HTTP on the free tier)
    int rc = http_get_to_buffer("http://ip-api.com/json/?fields=status,country,countryCode,city,lat,lon", g_buf, 8000);
    if (rc == 200) {
        JsonDocument doc(&g_psram_alloc);
        if (!deserializeJson(doc, g_buf.data(), g_buf.size()) && !strcmp(doc["status"] | "", "success")) {
            lat = doc["lat"].as<float>(); lon = doc["lon"].as<float>();
            snprintf(label, label_len, "%s, %s", doc["city"] | "?", doc["countryCode"] | "??");
            return true;
        }
    }
    // 2) ipwho.is (HTTPS)
    rc = http_get_to_buffer("https://ipwho.is/?fields=success,country_code,city,latitude,longitude", g_buf, 10000);
    if (rc == 200) {
        JsonDocument doc(&g_psram_alloc);
        if (!deserializeJson(doc, g_buf.data(), g_buf.size()) && (doc["success"] | false)) {
            lat = doc["latitude"].as<float>(); lon = doc["longitude"].as<float>();
            snprintf(label, label_len, "%s, %s", doc["city"] | "?", doc["country_code"] | "??");
            return true;
        }
    }
    return false;
}

bool geo_fetch_cities(const char* country_name, CityList& out) {
    out.count = 0;
    char enc[96]; url_encode(country_name, enc, sizeof(enc));
    char url[256];

    // 1) top cities by population
    snprintf(url, sizeof(url),
        "https://countriesnow.space/api/v0.1/countries/population/cities/filter/q"
        "?country=%s&limit=%d&order=dsc&orderBy=populationCounts", enc, MAX_CITIES);
    int rc = http_get_to_buffer(url, g_buf, 15000);
    if (rc == 200) {
        JsonDocument filter(&g_psram_alloc);
        filter["error"] = true; filter["data"][0]["city"] = true;
        JsonDocument doc(&g_psram_alloc);
        if (!deserializeJson(doc, g_buf.data(), g_buf.size(), DeserializationOption::Filter(filter)) && !(doc["error"] | true)) {
            for (JsonObject o : doc["data"].as<JsonArray>()) {
                if (out.count >= MAX_CITIES) break;
                const char* c = o["city"] | "";
                if (!c[0]) continue;
                char* dst = out.names[out.count];
                strlcpy(dst, c, CITY_NAME_LEN);
                char* par = strstr(dst, " (");             // "New York (NY)" → "New York"
                if (par) *par = 0;
                title_case(dst);
                // de-dup
                bool dup = false;
                for (int i = 0; i < out.count; i++) if (!strcasecmp(out.names[i], dst)) { dup = true; break; }
                if (!dup) out.count++;
            }
        }
    }
    if (out.count >= 5) return true;

    // 2) fallback: alphabetical full list, first MAX_CITIES
    snprintf(url, sizeof(url), "https://countriesnow.space/api/v0.1/countries/cities/q?country=%s", enc);
    rc = http_get_to_buffer(url, g_buf, 20000);
    if (rc != 200) return out.count > 0;
    JsonDocument doc(&g_psram_alloc);
    if (deserializeJson(doc, g_buf.data(), g_buf.size()) || (doc["error"] | true)) return out.count > 0;
    out.count = 0;
    for (JsonVariant v : doc["data"].as<JsonArray>()) {
        if (out.count >= MAX_CITIES) break;
        const char* c = v | "";
        if (!c[0]) continue;
        strlcpy(out.names[out.count++], c, CITY_NAME_LEN);
    }
    return out.count > 0;
}

bool geo_geocode(const char* city, const char* cc, float& lat, float& lon, char* label, size_t label_len) {
    char enc[128]; url_encode(city, enc, sizeof(enc));
    char url[320];
    snprintf(url, sizeof(url),
        "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=5&language=en&format=json%s%s",
        enc, (cc && cc[0]) ? "&countryCode=" : "", (cc && cc[0]) ? cc : "");
    int rc = http_get_to_buffer(url, g_buf, 10000);
    if (rc != 200) return false;
    JsonDocument doc(&g_psram_alloc);
    if (deserializeJson(doc, g_buf.data(), g_buf.size())) return false;
    JsonArray res = doc["results"].as<JsonArray>();
    if (res.isNull() || res.size() == 0) return false;
    // prefer the most populous match
    JsonObject best; long best_pop = -1;
    for (JsonObject o : res) {
        long pop = o["population"] | 0L;
        if (pop > best_pop) { best_pop = pop; best = o; }
    }
    lat = best["latitude"].as<float>(); lon = best["longitude"].as<float>();
    snprintf(label, label_len, "%s, %s", best["name"] | city, best["country_code"] | (cc ? cc : ""));
    return true;
}

bool geo_fetch_local_info(float lat, float lon, LocalInfo& out) {
    char url[200];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,weather_code,wind_speed_10m,wind_direction_10m,wind_gusts_10m"
        "&wind_speed_unit=ms&timezone=auto", lat, lon);
    int rc = http_get_to_buffer(url, g_buf, 10000);
    if (rc != 200) return false;
    JsonDocument doc(&g_psram_alloc);
    if (deserializeJson(doc, g_buf.data(), g_buf.size())) return false;
    out.utc_offset_seconds = doc["utc_offset_seconds"] | 0;
    out.temp_c   = doc["current"]["temperature_2m"] | 0.0f;
    out.wmo_code = doc["current"]["weather_code"] | 0;
    out.wind_mps = doc["current"]["wind_speed_10m"] | 0.0f;
    out.wind_dir = doc["current"]["wind_direction_10m"] | 0;
    out.gust_mps = doc["current"]["wind_gusts_10m"] | 0.0f;
    strlcpy(out.tz_name, doc["timezone"] | "", sizeof(out.tz_name));
    out.valid = true;
    return true;
}

const char* geo_wmo_short(int c) {
    if (c == 0) return "Clear";
    if (c <= 2) return "Fair";
    if (c == 3) return "Cloudy";
    if (c == 45 || c == 48) return "Fog";
    if (c >= 51 && c <= 57) return "Drizzle";
    if (c >= 61 && c <= 67) return "Rain";
    if (c >= 71 && c <= 77) return "Snow";
    if (c >= 80 && c <= 82) return "Showers";
    if (c >= 85 && c <= 86) return "Snow";
    if (c >= 95) return "Storm";
    return "";
}
