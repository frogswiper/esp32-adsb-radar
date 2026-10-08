#include "net_util.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

SpiRamAllocator g_psram_alloc;

bool PsramBuffer::reserve(size_t n) {
    if (n <= m_cap) return true;
    char* nb = (char*)heap_caps_realloc(m_buf, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!nb) return false;
    m_buf = nb; m_cap = n;
    return true;
}
void PsramBuffer::release() {
    if (m_buf) heap_caps_free(m_buf);
    m_buf = nullptr; m_cap = m_len = 0;
}
size_t PsramBuffer::write(const uint8_t* d, size_t n) {
    if (m_len + n + 1 > m_cap) {
        size_t want = (m_cap ? m_cap * 2 : 16384);
        while (want < m_len + n + 1) want *= 2;
        if (!reserve(want)) return 0;
    }
    memcpy(m_buf + m_len, d, n);
    m_len += n;
    m_buf[m_len] = 0;
    return n;
}

int http_get_to_buffer(const char* url, PsramBuffer& out, uint32_t timeout_ms) {
    out.clear();
    if (!out.reserve(16384)) return -100;

    bool https = strncmp(url, "https://", 8) == 0;
    WiFiClientSecure sclient;
    WiFiClient       pclient;
    if (https) sclient.setInsecure();

    HTTPClient http;
    http.setUserAgent("esp32-adsb-radar/" FW_VERSION " (JC4827W543)");
    http.setTimeout(timeout_ms);
    http.setConnectTimeout(timeout_ms);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setRedirectLimit(3);
    http.addHeader("Accept", "application/json");

    bool ok = https ? http.begin(sclient, url) : http.begin(pclient, url);
    if (!ok) return -101;

    int rc = http.GET();
    if (rc > 0) {
        http.writeToStream(&out);
        // make sure the buffer is terminated even when nothing was written
        if (out.size() == 0) out.write((const uint8_t*)"", 0);
    }
    http.end();
    return rc;
}

int http_post_json_to_buffer(const char* url, const char* body, PsramBuffer& out, uint32_t timeout_ms) {
    out.clear();
    if (!out.reserve(16384)) return -100;
    bool https = strncmp(url, "https://", 8) == 0;
    WiFiClientSecure sclient; WiFiClient pclient;
    if (https) sclient.setInsecure();
    HTTPClient http;
    http.setUserAgent("esp32-adsb-radar/" FW_VERSION " (JC4827W543)");
    http.setTimeout(timeout_ms); http.setConnectTimeout(timeout_ms);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    bool ok = https ? http.begin(sclient, url) : http.begin(pclient, url);
    if (!ok) return -101;
    int rc = http.POST((uint8_t*)body, strlen(body));
    if (rc > 0) { http.writeToStream(&out); if (out.size() == 0) out.write((const uint8_t*)"", 0); }
    http.end();
    return rc;
}

int http_post_to_buffer(const char* url, const char* body, const char* content_type, const char* const* headers, PsramBuffer& out, uint32_t timeout_ms) {
    out.clear();
    if (!out.reserve(4096)) return -100;
    bool https = strncmp(url, "https://", 8) == 0;
    WiFiClientSecure sclient; WiFiClient pclient;
    if (https) sclient.setInsecure();
    HTTPClient http;
    http.setUserAgent("esp32-adsb-radar/" FW_VERSION " (JC4827W543)");
    http.setTimeout(timeout_ms); http.setConnectTimeout(timeout_ms);
    http.addHeader("Content-Type", content_type);
    for (const char* const* h = headers; h && *h; h++) {
        const char* colon = strchr(*h, ':');
        if (!colon) continue;
        String name(*h, colon - *h); String val(colon + 1); val.trim();
        http.addHeader(name, val);
    }
    bool ok = https ? http.begin(sclient, url) : http.begin(pclient, url);
    if (!ok) return -101;
    int rc = http.POST((uint8_t*)body, strlen(body));
    if (rc > 0) { http.writeToStream(&out); if (out.size() == 0) out.write((const uint8_t*)"", 0); }
    http.end();
    return rc;
}

void url_encode(const char* src, char* dst, size_t dst_len) {
    static const char* hex = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char* p = (const unsigned char*)src; *p && o + 4 < dst_len; p++) {
        unsigned char c = *p;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') dst[o++] = c;
        else if (c == ' ') { dst[o++] = '%'; dst[o++] = '2'; dst[o++] = '0'; }
        else { dst[o++] = '%'; dst[o++] = hex[c >> 4]; dst[o++] = hex[c & 15]; }
    }
    dst[o] = 0;
}
