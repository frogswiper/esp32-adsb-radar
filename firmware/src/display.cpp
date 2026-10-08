// lvgl.h must come before LovyanGFX.hpp to prevent lv_font header redefinition
#include <lvgl.h>
#include "display.h"
#include "board_config.h"
#include <LovyanGFX.hpp>
#include <driver/ledc.h>
#include <Wire.h>
#define TOUCH_MODULES_GT911
#include <TouchLib.h>

// ── LovyanGFX board descriptor — QSPI NV3041A (no touch attached here) ───────
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_NV3041A  m_panel;
    lgfx::Bus_SPI        m_bus;

public:
    LGFX() {
        // ── QSPI Bus ─────────────────────────────────────────────────────────
        {
            auto cfg = m_bus.config();
            cfg.spi_host   = SPI2_HOST;
            cfg.freq_write = 32000000;
            cfg.freq_read  = 16000000;
            cfg.pin_sclk   = LCD_CLK;
            cfg.pin_io0    = LCD_D0;
            cfg.pin_io1    = LCD_D1;
            cfg.pin_io2    = LCD_D2;
            cfg.pin_io3    = LCD_D3;
            cfg.spi_3wire  = true;
            m_bus.config(cfg);
            m_panel.setBus(&m_bus);
        }
        // ── Panel ─────────────────────────────────────────────────────────────
        {
            auto cfg = m_panel.config();
            cfg.pin_cs  = LCD_CS;
            cfg.pin_rst = LCD_RST;
            m_panel.config(cfg);
        }
        setPanel(&m_panel);
    }
};

static LGFX tft;

// ── GT911 capacitive touch via TouchLib ──────────────────────────────────────
// TouchLib handles reset + I2C init internally.
// GT911_SLAVE_ADDRESS1 = 0x5D (INT LOW during reset)
static TouchLib touch(Wire, TOUCH_SDA, TOUCH_SCL, GT911_SLAVE_ADDRESS1, TOUCH_RST);
static bool g_touch_ok = false;

static void touch_init() {
    // Pull INT LOW before RST rises → GT911 latches address 0x5D
    pinMode(TOUCH_INT, OUTPUT);
    digitalWrite(TOUCH_INT, LOW);
    delay(10);

    // TouchLib init: resets the chip via TOUCH_RST and calls Wire.begin() internally
    g_touch_ok = touch.init();

    // Release INT so GT911 can drive it as interrupt output
    pinMode(TOUCH_INT, INPUT);

    Serial.printf("[Touch] init %s (addr 0x%02X)\n",
                  g_touch_ok ? "OK" : "FAILED", GT911_SLAVE_ADDRESS1);
}

// ── LVGL buffers (double-buffered, allocated in PSRAM at init) ────────────────
static const uint32_t LVGL_BUF_PIXELS = DISPLAY_WIDTH * (DISPLAY_HEIGHT / 10);
static lv_color_t*    buf1 = nullptr;
static lv_color_t*    buf2 = nullptr;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t      disp_drv;
static lv_indev_drv_t     touch_drv;

// ── Flush callback ────────────────────────────────────────────────────────────
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    // This NV3041A shows every colour inverted (black renders white) and ignores the
    // panel INVON/INVOFF command, so invert in software before pushing the pixels.
    uint32_t n = w * h;
    for (uint32_t i = 0; i < n; i++) px[i].full ^= 0xFFFF;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.writePixels((lgfx::rgb565_t*)px, w * h);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

// ── Touch read callback ───────────────────────────────────────────────────────
// GT911 reports native landscape coords (x: 0-479, y: 0-271).
// Display is rotated 90° CW → portrait 272×480.
// Mapping: lvgl_x = (DISPLAY_HEIGHT_NATIVE-1) - raw_y
//          lvgl_y = raw_x
static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    static bool was_pressed = false;
    if (g_touch_ok && touch.read()) {
        TP_Point p = touch.getPoint(0);
        data->point.x = (lv_coord_t)p.y;
        data->point.y = (lv_coord_t)((DISPLAY_HEIGHT - 1) - p.x);
        data->state   = LV_INDEV_STATE_PR;
        if (!was_pressed) Serial.printf("[Touch] press at %d,%d (raw %d,%d) t=%lu\n", data->point.x, data->point.y, p.x, p.y, (unsigned long)millis());
        was_pressed = true;
    } else {
        data->state = LV_INDEV_STATE_REL;
        was_pressed = false;
    }
}

// ── Backlight via LEDC ────────────────────────────────────────────────────────
void display_set_brightness(uint8_t value) {
    ledcWrite(TFT_BL_LEDC_CHANNEL, value);
}

// ── Public init ───────────────────────────────────────────────────────────────
void display_init() {
    // Backlight on via LEDC
    ledcSetup(TFT_BL_LEDC_CHANNEL, TFT_BL_LEDC_FREQ, TFT_BL_LEDC_RES);
    ledcAttachPin(TFT_BL, TFT_BL_LEDC_CHANNEL);
    ledcWrite(TFT_BL_LEDC_CHANNEL, 200);  // ~78% brightness at startup

    // Allocate LVGL render buffers in PSRAM
    buf1 = (lv_color_t*)ps_malloc(LVGL_BUF_PIXELS * sizeof(lv_color_t));
    buf2 = (lv_color_t*)ps_malloc(LVGL_BUF_PIXELS * sizeof(lv_color_t));

    // Init LovyanGFX (display only — touch handled separately below)
    tft.init();
    tft.setRotation(1);   // 90° CW → portrait 272×480
    tft.fillScreen(TFT_BLACK);

    // Init GT911 touch AFTER display is up (avoids GPIO conflict during display init)
    touch_init();

    // Init LVGL
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LVGL_BUF_PIXELS);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = DISPLAY_WIDTH;
    disp_drv.ver_res  = DISPLAY_HEIGHT;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_t* disp = lv_disp_drv_register(&disp_drv);
    // green monochrome theme
    lv_theme_t* th = lv_theme_default_init(disp, lv_color_hex(0x1FA84A), lv_color_hex(0x0E5A27), true, &lv_font_montserrat_14);
    lv_disp_set_theme(disp, th);

    lv_indev_drv_init(&touch_drv);
    touch_drv.type    = LV_INDEV_TYPE_POINTER;
    touch_drv.read_cb = lvgl_touch_cb;
    lv_indev_drv_register(&touch_drv);

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), LV_PART_MAIN);
}
