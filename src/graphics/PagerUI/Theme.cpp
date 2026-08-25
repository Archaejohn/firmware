#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include "Theme.h"

namespace PagerUI
{
namespace Theme
{

// The panel is an inverted IPS; dark reads better on it and costs less backlight.
static const Palette kDark = {
    .background = LV_COLOR_MAKE(0x0E, 0x11, 0x16),
    .surface = LV_COLOR_MAKE(0x1A, 0x1F, 0x27),
    .surfaceAlt = LV_COLOR_MAKE(0x24, 0x2B, 0x35),
    .text = LV_COLOR_MAKE(0xE8, 0xED, 0xF2),
    .textDim = LV_COLOR_MAKE(0x8A, 0x94, 0xA3),
    .divider = LV_COLOR_MAKE(0x2E, 0x37, 0x42),
    .accent = LV_COLOR_MAKE(0x12, 0xA1, 0x50),
    .accentBright = LV_COLOR_MAKE(0x22, 0xC5, 0x5E),
    .ok = LV_COLOR_MAKE(0x22, 0xC5, 0x5E),
    .pending = LV_COLOR_MAKE(0x8A, 0x94, 0xA3),
    .error = LV_COLOR_MAKE(0xEF, 0x44, 0x44),
    .warning = LV_COLOR_MAKE(0xF5, 0x9E, 0x0B),
    .secure = LV_COLOR_MAKE(0x3B, 0x82, 0xF6),
};

static const Palette kLight = {
    .background = LV_COLOR_MAKE(0xF7, 0xF9, 0xFB),
    .surface = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .surfaceAlt = LV_COLOR_MAKE(0xE9, 0xEE, 0xF3),
    .text = LV_COLOR_MAKE(0x11, 0x18, 0x21),
    .textDim = LV_COLOR_MAKE(0x5B, 0x66, 0x74),
    .divider = LV_COLOR_MAKE(0xD5, 0xDD, 0xE5),
    .accent = LV_COLOR_MAKE(0x0F, 0x8B, 0x45),
    .accentBright = LV_COLOR_MAKE(0x16, 0xA3, 0x4A),
    .ok = LV_COLOR_MAKE(0x16, 0xA3, 0x4A),
    .pending = LV_COLOR_MAKE(0x6B, 0x74, 0x80),
    .error = LV_COLOR_MAKE(0xDC, 0x26, 0x26),
    .warning = LV_COLOR_MAKE(0xD9, 0x77, 0x06),
    .secure = LV_COLOR_MAKE(0x25, 0x63, 0xEB),
};

static bool dark = true;

const Palette &palette()
{
    return dark ? kDark : kLight;
}

bool isDark()
{
    return dark;
}

void setDark(bool value)
{
    if (dark == value)
        return;
    dark = value;
    // Styles read the palette by reference at apply time, so a repaint is all that is needed.
    lv_obj_report_style_change(nullptr);
}

lv_color_t colorForNode(uint32_t nodeNum)
{
    // Knuth's multiplicative hash spreads adjacent node numbers - which are common on a mesh
    // built from sequential hardware - across the hue circle instead of clustering them.
    const uint8_t hue = (uint8_t)((nodeNum * 2654435761u) >> 24);

    // Fixed saturation and value so every avatar carries the same weight next to the text,
    // and so no node can land on something unreadable.
    return lv_color_hsv_to_rgb((uint16_t)((hue * 360u) / 256u), 60, dark ? 70 : 60);
}

} // namespace Theme
} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
