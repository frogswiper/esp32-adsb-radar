#include "ntp.h"
#include "settings.h"
#include <Arduino.h>
#include <time.h>

static bool g_synced = false;

// Build a POSIX TZ string from UTC offset seconds
// e.g.  3600 → "UTC-1"    (POSIX sign is inverted vs. common convention)
//      -18000 → "UTC5"
static void apply_posix_tz(int utc_offset_seconds) {
    int total_min = utc_offset_seconds / 60;
    int h = -(total_min / 60);      // negate for POSIX convention
    int m = abs(total_min % 60);
    char tz[32];
    if (m == 0) snprintf(tz, sizeof(tz), "UTC%d", h);
    else        snprintf(tz, sizeof(tz), "UTC%d:%02d", h, m);
    setenv("TZ", tz, 1);
    tzset();
}

void ntp_sync() {
    // Apply saved timezone offset so local time is correct after sync
    apply_posix_tz(settings_get().utc_offset_seconds);

    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    struct tm info;
    uint32_t start = millis();
    while (!getLocalTime(&info, 1000) && millis() - start < 10000) {
        delay(100);
    }
    g_synced = (millis() - start < 10000);
}

void ntp_apply_offset(int utc_offset_seconds) {
    apply_posix_tz(utc_offset_seconds);
}

bool ntp_is_synced() { return g_synced; }

struct tm ntp_get_local_time() {
    struct tm info = {};
    getLocalTime(&info, 100);
    return info;
}
