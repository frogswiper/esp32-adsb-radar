#pragma once
#include <stdint.h>
#include <stdbool.h>

// ISS position (wheretheiss.at) and METAR (aviationweather.gov) — fetched by the network task.
struct IssState { bool valid; float lat, lon, alt_km, vel_kmh; uint32_t fetched_ms; float dist_km, bearing; };
struct MetarState { bool valid; char station[6]; char raw[160]; uint32_t fetched_ms; };

bool extras_fetch_iss(IssState& out);
bool extras_fetch_metar(const char* icao, MetarState& out);
const IssState&   extras_iss();
const MetarState& extras_metar();
void extras_set_iss(const IssState& s);
void extras_set_metar(const MetarState& m);
// nearest airport ICAO for METAR (prefers larger fields), "" if none within 200 km
const char* extras_metar_station(float lat, float lon);
