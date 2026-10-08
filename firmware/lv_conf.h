/**
 * lv_conf.h — LVGL configuration for ESP32 ADS-B Radar (JC4827W543)
 * Portrait mode: 272 x 480
 */
#if 1  /* Set to 1 to enable */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/*====================
   MEMORY SETTINGS
 *====================*/
/* Use custom malloc so LVGL heap comes from the FreeRTOS heap
   (PSRAM-backed on boards with BOARD_HAS_PSRAM) rather than
   a 256 KB static BSS array that overflows internal DRAM. */
#define LV_MEM_CUSTOM          1
#define LV_MEM_CUSTOM_INCLUDE  <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC    malloc
#define LV_MEM_CUSTOM_REALLOC  realloc
#define LV_MEM_CUSTOM_FREE     free
/* LV_MEM_SIZE is ignored when LV_MEM_CUSTOM=1 */
#define LV_MEM_SIZE (48 * 1024U)

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DISP_DEF_REFR_PERIOD 16  /* ~60fps */
#define LV_INDEV_DEF_READ_PERIOD 16

/* Tick source: use Arduino millis() so LVGL timers actually fire */
#define LV_TICK_CUSTOM          1
#define LV_TICK_CUSTOM_INCLUDE  <Arduino.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR ((uint32_t)millis())

/*====================
   FEATURE CONFIGURATION
 *====================*/
#define LV_DPI_DEF 130

/*=================
 * OPERATING SYSTEM
 *=================*/
#define LV_OS_NONE

/*========================
 * RENDERING CONFIGURATION
 *========================*/
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN        4

/*=======================
 * INPUT DEVICE SETTINGS
 *=======================*/
#define LV_INDEV_DEF_LONG_PRESS_TIME    400
#define LV_INDEV_DEF_LONG_PRESS_REP_TIME 100

/*==================
 * FONT USAGE
 *==================*/
#define LV_FONT_MONTSERRAT_10  1
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_18  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_28  1
#define LV_FONT_MONTSERRAT_32  1
#define LV_FONT_MONTSERRAT_36  1
#define LV_FONT_MONTSERRAT_48  1

#define LV_FONT_UNSCII_8   1
#define LV_FONT_UNSCII_16  1

#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Enable symbol font (for tab icons etc.) */
#define LV_SYMBOL_DEF_FONT 1

/*===================
 * WIDGET USAGE
 *===================*/
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     1
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TABLE      0
#define LV_USE_ANIMIMG    0
#define LV_USE_TEXTAREA   1

/*==================
 * EXTRA COMPONENTS
 *==================*/
#define LV_USE_TABVIEW    1
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    1
#define LV_USE_CALENDAR   0
#define LV_USE_KEYBOARD   1
#define LV_USE_COLORWHEEL 0
#define LV_USE_LED        1
#define LV_USE_LIST       1
#define LV_USE_MENU       0
#define LV_USE_METER      0
#define LV_USE_MSGBOX     1
#define LV_USE_CHART      0
#define LV_USE_QRCODE     0
#define LV_USE_SNAPSHOT   1   /* screenshot over serial (debug) */

/*==================
 * THEMES
 *==================*/
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1   /* Dark theme (radar) */
#define LV_USE_THEME_SIMPLE     1
#define LV_USE_THEME_MONO       0

/*==================
 * LOGGING
 *==================*/
#define LV_USE_LOG 0

/*==================
 * ASSERT
 *==================*/
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

#endif /* LV_CONF_H */
#endif /* End of "Content enable" */
