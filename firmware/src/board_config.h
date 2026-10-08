#pragma once

/**
 * board_config.h — Pin definitions for JC4827W543
 *
 * JC4827W543: ESP32-S3, 4.3" 480×272 NV3041A LCD controller, capacitive touch GT911
 * Display interface: Quad SPI (QSPI) — NOT RGB parallel
 * Display is used in PORTRAIT mode → 272×480
 */

// ── QSPI interface to NV3041A LCD controller ─────────────────────────────────
#define LCD_CS      45
#define LCD_CLK     47
#define LCD_D0      21
#define LCD_D1      48
#define LCD_D2      40
#define LCD_D3      39
#define LCD_RST     (-1)   // not connected

// ── Backlight ─────────────────────────────────────────────────────────────────
#define TFT_BL              1
#define TFT_BL_LEDC_CHANNEL 0
#define TFT_BL_LEDC_FREQ    5000
#define TFT_BL_LEDC_RES     8      // 8-bit → 0-255

// ── Capacitive touch (GT911 I2C) ─────────────────────────────────────────────
#define TOUCH_SDA    8
#define TOUCH_SCL    4
#define TOUCH_INT    3
#define TOUCH_RST    38

// ── Display resolution ─────────────────────────────────────────────────────
// Native landscape: 480 × 272
// We rotate 90° → portrait: 272 × 480
#define DISPLAY_WIDTH_NATIVE  480
#define DISPLAY_HEIGHT_NATIVE 272
#define DISPLAY_WIDTH         272   // portrait
#define DISPLAY_HEIGHT        480   // portrait

// ── UI layout constants ────────────────────────────────────────────────────
#define TAB_BAR_HEIGHT    60
#define STATUS_BAR_HEIGHT  0
#define CONTENT_HEIGHT   (DISPLAY_HEIGHT - TAB_BAR_HEIGHT)

// ── ADS-B Radar layout (radar page has no nav bar: 60 % radar / 40 % info) ──
#define RADAR_H           288                      // 60 % of 480
#define RADAR_W           DISPLAY_WIDTH            // 272
#define BOTTOM_Y          RADAR_H
#define BOTTOM_H          (DISPLAY_HEIGHT - RADAR_H)   // 192
#define RADAR_CX          136
#define RADAR_CY          152
#define RADAR_R_TICK      134                      // bezel outer radius
#define RADAR_R_LABEL     118                      // degree label radius
#define RADAR_R_MAX       108                      // outer range ring
