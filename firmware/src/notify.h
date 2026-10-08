#pragma once
#include <stdint.h>
#include <stddef.h>

// Alerts: logged on the device (ring buffer, served by the web panel) and pushed to ntfy.sh / webhook
// via the network task. Rate-limited per key (e.g. ICAO hex) and kind.
enum NotifyKind : uint8_t { NK_EMERGENCY, NK_WATCHLIST, NK_OVERHEAD, NK_INFO };

struct AlertEntry { uint32_t epoch; uint32_t ms; uint8_t kind; char title[40]; char message[96]; };
#define ALERT_LOG_SIZE 40

void notify_init();
// Returns true when the alert was new (not rate-limited) and was logged / queued.
bool notify_event(NotifyKind kind, const char* key, const char* title, const char* message);
// Executed on the network task: deliver one alert to ntfy + webhook.
void notify_deliver(const char* title, const char* message, NotifyKind kind);
int  notify_log_count();
const AlertEntry& notify_log_get(int i);   // 0 = newest
const char* notify_kind_name(NotifyKind k);
