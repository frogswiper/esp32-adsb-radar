#pragma once
#include <time.h>
#include <stdbool.h>

void      ntp_sync();
void      ntp_apply_offset(int utc_offset_seconds);
bool      ntp_is_synced();
struct tm ntp_get_local_time();
