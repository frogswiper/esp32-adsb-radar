#include "notify.h"
#include "settings.h"
#include "net_util.h"
#include "net_task.h"
#include <Arduino.h>
#include <time.h>
#include <string.h>

static AlertEntry g_log[ALERT_LOG_SIZE];
static int g_log_n = 0, g_log_head = 0;

struct Recent { char key[12]; uint8_t kind; uint32_t ms; };
static Recent g_recent[48];
static int g_recent_n = 0;
static const uint32_t REPEAT_MS = 30UL * 60UL * 1000UL;

void notify_init() { memset(g_log, 0, sizeof(g_log)); }

const char* notify_kind_name(NotifyKind k) {
    switch (k) { case NK_EMERGENCY: return "emergency"; case NK_WATCHLIST: return "watchlist"; case NK_OVERHEAD: return "overhead"; default: return "info"; }
}

static bool rate_limited(NotifyKind kind, const char* key) {
    uint32_t now = millis();
    for (int i = 0; i < g_recent_n; i++)
        if (g_recent[i].kind == kind && !strcmp(g_recent[i].key, key)) {
            if (now - g_recent[i].ms < REPEAT_MS) return true;
            g_recent[i].ms = now; return false;
        }
    int slot = g_recent_n < 48 ? g_recent_n++ : 0;
    if (g_recent_n == 48) { uint32_t oldest = now; for (int i = 0; i < 48; i++) if (g_recent[i].ms < oldest) { oldest = g_recent[i].ms; slot = i; } }
    strlcpy(g_recent[slot].key, key, sizeof(g_recent[slot].key)); g_recent[slot].kind = kind; g_recent[slot].ms = now;
    return false;
}

bool notify_event(NotifyKind kind, const char* key, const char* title, const char* message) {
    if (rate_limited(kind, key)) return false;
    AlertEntry& e = g_log[g_log_head];
    e.epoch = (uint32_t)time(nullptr); e.ms = millis(); e.kind = kind;
    strlcpy(e.title, title, sizeof(e.title)); strlcpy(e.message, message, sizeof(e.message));
    g_log_head = (g_log_head + 1) % ALERT_LOG_SIZE; if (g_log_n < ALERT_LOG_SIZE) g_log_n++;
    Serial.printf("[ALERT] %s: %s - %s\n", notify_kind_name(kind), title, message);
    const Settings& s = settings_get();
    bool push = (kind == NK_EMERGENCY && s.notify_emergency) || (kind == NK_WATCHLIST && s.notify_watch) || (kind == NK_OVERHEAD && s.notify_alert);
    if (push && (s.ntfy_topic[0] || s.webhook_url[0])) net_send_notify(title, message, kind);
    return true;
}

int notify_log_count() { return g_log_n; }
const AlertEntry& notify_log_get(int i) {
    int idx = (g_log_head - 1 - i + ALERT_LOG_SIZE * 2) % ALERT_LOG_SIZE;
    return g_log[idx];
}

static PsramBuffer g_buf;
void notify_deliver(const char* title, const char* message, NotifyKind kind) {
    const Settings& s = settings_get();
    if (s.ntfy_topic[0]) {
        char url[96]; snprintf(url, sizeof(url), "https://ntfy.sh/%s", s.ntfy_topic);
        char htitle[64]; snprintf(htitle, sizeof(htitle), "Title: %s", title);
        char htags[48];  snprintf(htags, sizeof(htags), "Tags: %s", kind == NK_EMERGENCY ? "rotating_light" : kind == NK_WATCHLIST ? "star" : kind == NK_OVERHEAD ? "airplane" : "information_source");
        const char* hprio = kind == NK_EMERGENCY ? "Priority: high" : "Priority: default";
        const char* headers[] = {htitle, htags, hprio, nullptr};
        int rc = http_post_to_buffer(url, message, "text/plain; charset=utf-8", headers, g_buf, 8000);
        Serial.printf("[NTFY] %d\n", rc);
    }
    if (s.webhook_url[0]) {
        char body[320];
        snprintf(body, sizeof(body), "{\"source\":\"esp32-adsb-radar\",\"kind\":\"%s\",\"title\":\"%s\",\"message\":\"%s\"}", notify_kind_name(kind), title, message);
        int rc = http_post_to_buffer(s.webhook_url, body, "application/json", nullptr, g_buf, 8000);
        Serial.printf("[HOOK] %d\n", rc);
    }
}
