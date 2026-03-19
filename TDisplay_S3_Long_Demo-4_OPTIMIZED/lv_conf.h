/**
 * lv_conf.h  —  LVGL v8.3.5 for T-Display-S3-Long (ESP32-S3)
 * Place in the SAME folder as your .ino
 */

#if 1

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ── Color depth ─────────────────────────────────────────────── */
#define LV_COLOR_DEPTH          16
#define LV_COLOR_16_SWAP        0
#define LV_COLOR_SCREEN_TRANSP  0

/* ── Memory ──────────────────────────────────────────────────── */
#define LV_MEM_CUSTOM           0
#define LV_MEM_SIZE            (64 * 1024U)
#define LV_MEM_BUF_MAX_NUM      16
#define LV_MEM_ADR              0
#define LV_MEM_AUTO_OPT         1

/* ── HAL ─────────────────────────────────────────────────────── */
#define LV_DISP_DEF_REFR_PERIOD   30
#define LV_INDEV_DEF_READ_PERIOD  30

/* ── Tick ─────────────────────────────────────────────────────── */
#define LV_TICK_CUSTOM          1
#if LV_TICK_CUSTOM
  #define LV_TICK_CUSTOM_INCLUDE <Arduino.h>
  #define LV_TICK_CUSTOM_SYS_TIME_EXPR ((uint32_t)millis())
#endif

#define LV_USE_BTNMATRIX    1   // was 0 — required by lv_msgbox
#define LV_USE_IMG          1   // was 0 — required by lv_animimg

/* ── Logging ─────────────────────────────────────────────────── */
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1
#define LV_LOG_TRACE_MEM        0
#define LV_LOG_TRACE_TIMER      0
#define LV_LOG_TRACE_INDEV      0
#define LV_LOG_TRACE_DISP_REFR  0
#define LV_LOG_TRACE_EVENT      0
#define LV_LOG_TRACE_OBJ_CREATE 0
#define LV_LOG_TRACE_LAYOUT     0
#define LV_LOG_TRACE_ANIM       0

/* ── Assert ──────────────────────────────────────────────────── */
#define LV_USE_ASSERT_NULL      1
#define LV_USE_ASSERT_MALLOC    1
#define LV_USE_ASSERT_STYLE     0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ       0

/* ── Debug ───────────────────────────────────────────────────── */
#define LV_USE_PERF_MONITOR     0
#define LV_USE_MEM_MONITOR      0
#define LV_USE_REFR_DEBUG       0

/* ── Compiler ────────────────────────────────────────────────── */
#define LV_ATTRIBUTE_MEM_FAST
#define LV_ATTRIBUTE_FAST_MEM
#define LV_ATTRIBUTE_DMA
#define LV_EXPORT_CONST_INT(int_value)  extern const int int_value
#define LV_USE_LARGE_COORD      0

/* ── Fonts ───────────────────────────────────────────────────── */
#define LV_FONT_MONTSERRAT_8    0
#define LV_FONT_MONTSERRAT_10   0
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   0
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_22   0
#define LV_FONT_MONTSERRAT_24   1
#define LV_FONT_MONTSERRAT_26   0
#define LV_FONT_MONTSERRAT_28   0
#define LV_FONT_MONTSERRAT_30   0
#define LV_FONT_MONTSERRAT_32   1
#define LV_FONT_MONTSERRAT_34   0
#define LV_FONT_MONTSERRAT_36   0
#define LV_FONT_MONTSERRAT_38   0
#define LV_FONT_MONTSERRAT_40   0
#define LV_FONT_MONTSERRAT_42   0
#define LV_FONT_MONTSERRAT_44   0
#define LV_FONT_MONTSERRAT_46   0
#define LV_FONT_MONTSERRAT_48   0

#define LV_FONT_MONTSERRAT_12_SUBPX      0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK            0
#define LV_FONT_UNSCII_8                 0
#define LV_FONT_UNSCII_16                0
#define LV_FONT_CUSTOM_DECLARE
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_FONT_FMT_TXT_LARGE    0
#define LV_USE_FONT_SUBPX        0
#define LV_FONT_SUBPX_BGR        0

/* ── Text ────────────────────────────────────────────────────── */
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_"
#define LV_TXT_LINE_BREAK_LONG_LEN     0
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN  3
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3
#define LV_TXT_COLOR_CMD '#'
#define LV_USE_BIDI         0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/* ── Widgets ─────────────────────────────────────────────────── */
#define LV_USE_ARC          1
#define LV_USE_BAR          1
#define LV_USE_BTN          1
#define LV_USE_BTNMATRIX    0
#define LV_USE_CALENDAR     0
#define LV_USE_CANVAS       0
#define LV_USE_CHART        0
#define LV_USE_CHECKBOX     0
#define LV_USE_COLORWHEEL   0
#define LV_USE_DROPDOWN     1
#define LV_USE_IMG          0
#define LV_USE_IMGBTN       0
#define LV_USE_KEYBOARD     0
#define LV_USE_LABEL        1
#define LV_USE_LED          0
#define LV_USE_LINE         0
#define LV_USE_LIST         1
#define LV_USE_MENU         0
#define LV_USE_METER        0
#define LV_USE_MSGBOX       1
#define LV_USE_ROLLER       0
#define LV_USE_SLIDER       0
#define LV_USE_SPAN         0
#define LV_USE_SPINBOX      0
#define LV_USE_SPINNER      0
#define LV_USE_SWITCH       0
#define LV_USE_TABLE        0
#define LV_USE_TABVIEW      0
#define LV_USE_TILEVIEW     1
#define LV_USE_WIN          0

/* ── Theme ───────────────────────────────────────────────────── */
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1
#define LV_THEME_DEFAULT_GROW   0
#define LV_THEME_DEFAULT_TRANSITION_TIME 80
#define LV_USE_THEME_SIMPLE     0
#define LV_USE_THEME_MONO       0

/* ── Layouts ─────────────────────────────────────────────────── */
#define LV_USE_FLEX     1
#define LV_USE_GRID     0

/* ── 3rd party ───────────────────────────────────────────────── */
#define LV_USE_FS_STDIO     0
#define LV_USE_FS_POSIX     0
#define LV_USE_FS_WIN32     0
#define LV_USE_FS_FATFS     0
#define LV_USE_PNG          0
#define LV_USE_BMP          0
#define LV_USE_SJPG         0
#define LV_USE_GIF          0
#define LV_USE_QRCODE       0
#define LV_USE_FREETYPE     0
#define LV_USE_RLOTTIE      0
#define LV_USE_FFMPEG       0

/* ── Misc ────────────────────────────────────────────────────── */
#define LV_USE_USER_DATA        1
#define LV_USE_GESTURE_RECOGNITION 0
#define LV_SPRINTF_CUSTOM       0
#define LV_SPRINTF_USE_FLOAT    0
#define LV_USE_OS               LV_OS_NONE
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN
#define LV_BUILD_TEST           0

#endif /* LV_CONF_H */
#endif /* Enable */
