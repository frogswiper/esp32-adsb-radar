#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// ArduinoJson allocator backed by PSRAM so big API replies never touch internal DRAM.
struct SpiRamAllocator : ArduinoJson::Allocator {
    void* allocate(size_t n) override            { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
    void  deallocate(void* p) override           { heap_caps_free(p); }
    void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
};
extern SpiRamAllocator g_psram_alloc;

// Growable byte buffer in PSRAM that HTTPClient::writeToStream() can write into.
class PsramBuffer : public Stream {
public:
    ~PsramBuffer() { release(); }
    bool reserve(size_t n);
    void release();
    void clear() { m_len = 0; }
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* d, size_t n) override;
    int available() override { return 0; }
    int read() override      { return -1; }
    int peek() override      { return -1; }
    const char* data() const { return m_buf ? m_buf : ""; }
    size_t size() const      { return m_len; }
private:
    char*  m_buf = nullptr;
    size_t m_cap = 0, m_len = 0;
};

// HTTP(S) GET into a PSRAM buffer. Returns HTTP status (or negative HTTPClient error).
// `out` is NUL-terminated on success. Follows up to 3 redirects.
int http_get_to_buffer(const char* url, PsramBuffer& out, uint32_t timeout_ms = 12000);

// HTTP(S) POST of a JSON body into a PSRAM buffer. Returns HTTP status.
int http_post_json_to_buffer(const char* url, const char* body, PsramBuffer& out, uint32_t timeout_ms = 12000);

// Generic POST with content type and optional extra headers (array of "Name: value", nullptr-terminated).
int http_post_to_buffer(const char* url, const char* body, const char* content_type, const char* const* headers, PsramBuffer& out, uint32_t timeout_ms = 10000);

// URL-encode (RFC 3986 unreserved kept) into dst.
void url_encode(const char* src, char* dst, size_t dst_len);
