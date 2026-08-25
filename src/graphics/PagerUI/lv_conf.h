/**
 * LVGL configuration for PagerUI (LilyGo T-Lora Pager, 480x222 ST7796).
 *
 * Reached via -D LV_CONF_INCLUDE_SIMPLE plus -I src/graphics/PagerUI.
 *
 * This is deliberately not a copy of lv_conf_template.h. LVGL's lv_conf_internal.h supplies
 * a default for every option it knows about, so only the settings that matter for this board
 * are stated here - the rest track upstream defaults instead of silently going stale against
 * a vendored 800-line copy.
 */

#pragma once

/* ---------------------------------------------------------------------------------------
 * Colour and refresh
 * ------------------------------------------------------------------------------------- */

#define LV_COLOR_DEPTH 16

/* ~33 fps. The panel is driven over a bus shared with the LoRa radio, so there is no point
 * asking for more than the flush path can deliver. */
#define LV_DEF_REFR_PERIOD 30

/* 480x222 across roughly 2.4in of glass. */
#define LV_DPI_DEF 130

/* ---------------------------------------------------------------------------------------
 * Memory
 *
 * The widget tree, styles and draw descriptors are latency-tolerant, so they live in PSRAM
 * and stay out of the internal heap that BLE and the radio compete for. The draw buffers are
 * the exception and are allocated separately from internal DMA RAM - see LvglPort.cpp.
 * ------------------------------------------------------------------------------------- */

#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (256 * 1024U)
#define LV_MEM_POOL_EXPAND_SIZE 0
#define LV_MEM_ADR 0
#define LV_MEM_POOL_INCLUDE "esp_heap_caps.h"
#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM)

/* ---------------------------------------------------------------------------------------
 * Threading
 *
 * LVGL is used from exactly one task (see UiTask.h), so its own locking is not needed and
 * would cost a mutex round-trip on every timer tick. The invariant is enforced by keeping
 * every lv_* call on the UI task rather than by a lock.
 * ------------------------------------------------------------------------------------- */

#define LV_USE_OS LV_OS_NONE

/* ---------------------------------------------------------------------------------------
 * Rendering
 * ------------------------------------------------------------------------------------- */

/* Required for rounded corners, which the chat bubbles and avatars depend on. */
#define LV_DRAW_SW_COMPLEX 1
#define LV_DRAW_SW_DRAW_UNIT_CNT 1

/* ---------------------------------------------------------------------------------------
 * Logging - routed to the normal serial console at WARN and above.
 * ------------------------------------------------------------------------------------- */

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* Cheap, and they catch real mistakes early. */
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

/* ---------------------------------------------------------------------------------------
 * Fonts - the main flash lever, roughly 20-32 kB each.
 *
 * Four sizes cover the whole UI: 12 timestamps and badges, 14 body and previews, 16 names
 * and bubble text, 20 headers and avatar initials. Every other built-in size is switched off
 * explicitly, because several are enabled by default.
 * ------------------------------------------------------------------------------------- */

#define LV_FONT_MONTSERRAT_8 0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* No subpixel rendering (the panel is not RGB-striped in a known order) and no compressed
 * fonts (decompression on every glyph is not worth the flash saved). */
#define LV_USE_FONT_SUBPX 0
#define LV_USE_FONT_COMPRESSED 0
#define LV_FONT_FMT_TXT_LARGE 0

/* ---------------------------------------------------------------------------------------
 * Widgets
 *
 * On: what the chat, contact, settings and map screens actually use.
 * Off: everything else, to keep the flash and the attack surface down.
 * ------------------------------------------------------------------------------------- */

#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BUTTON 1
#define LV_USE_CANVAS 1 /* map rendering */
#define LV_USE_LABEL 1
#define LV_USE_LINE 1
#define LV_USE_LIST 1
#define LV_USE_MENU 1
#define LV_USE_MSGBOX 1
#define LV_USE_SLIDER 1
#define LV_USE_SPINNER 1
#define LV_USE_SWITCH 1
#define LV_USE_TEXTAREA 1

#define LV_USE_ANIMIMG 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGE 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0 /* the pager has a real one */
#define LV_USE_LED 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

/* ---------------------------------------------------------------------------------------
 * Everything we do not ship: decoders, file systems, third-party renderers, profiling.
 * ------------------------------------------------------------------------------------- */

#define LV_USE_BMP 0
#define LV_USE_FFMPEG 0
#define LV_USE_FREETYPE 0
#define LV_USE_GIF 0
#define LV_USE_LODEPNG 0
#define LV_USE_LZ4 0
#define LV_USE_QRCODE 0
#define LV_USE_RLE 0
#define LV_USE_SVG 0
#define LV_USE_THORVG_INTERNAL 0
#define LV_USE_TINY_TTF 0
#define LV_USE_VECTOR_GRAPHIC 0

#define LV_USE_FS_FATFS 0
#define LV_USE_FS_LITTLEFS 0
#define LV_USE_FS_MEMFS 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_WIN32 0

#define LV_USE_MEM_MONITOR 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_PROFILER 0
#define LV_USE_REFR_DEBUG 0
#define LV_USE_SYSMON 0

/* No simulator/driver backends - LvglPort.cpp registers the display itself. */
#define LV_USE_SDL 0
#define LV_USE_X11 0
#define LV_USE_LINUX_DRM 0
#define LV_USE_LINUX_FBDEV 0
