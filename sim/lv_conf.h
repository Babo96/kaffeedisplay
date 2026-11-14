#ifndef LV_CONF_H
#define LV_CONF_H

// Minimal LVGL 9 configuration for the host simulator. The firmware uses the
// ESP-IDF Kconfig (sdkconfig.defaults) instead.

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_ARGB8888
#define LV_MEM_SIZE (4 * 1024 * 1024)
#define LV_USE_STDLIB_MALLOC 1 /* LV_STDLIB_CLIB */
#define LV_USE_OS 0            /* LV_OS_NONE */
#define LV_DEF_REFR_PERIOD 16
#define LV_DPI_DEF 130

// I4 avatars and the SVG-derived I8 cup use ARGB8888 palettes in LVGL 9.6.
#define LV_DRAW_SW_SUPPORT_ARGB8888 1

#define LV_USE_QRCODE 1
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_48 0

#define LV_USE_LOG 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_SDL 0

#endif /* LV_CONF_H */
