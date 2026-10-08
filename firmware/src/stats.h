#pragma once
#include <stdint.h>

// Session + daily statistics, persisted to NVS every few minutes.
#define STATS_TOP 12
struct TopEntry { char key[8]; uint16_t count; };
struct Stats {
    uint16_t hourly[24];       // unique aircraft first seen per local hour (today)
    uint32_t unique_today;     // unique ICAO hex today
    uint16_t max_tracked;      // most aircraft tracked at once
    int32_t  max_alt_ft;  char max_alt_who[10];
    int16_t  max_gs_kt;   char max_gs_who[10];
    float    min_dist_km; char min_dist_who[10];
    uint16_t emergencies, alerts;
    TopEntry airlines[STATS_TOP];
    TopEntry types[STATS_TOP];
    uint16_t day_key;          // yyyyddd-ish, resets the daily numbers
    uint32_t boot_count;
};
void   stats_init();
void   stats_tick();                 // call after every successful data update
void   stats_note_alert();
void   stats_note_emergency();
const Stats& stats_get();
void   stats_save_if_due();          // periodic persistence
void   stats_reset();
