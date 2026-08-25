#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include <lvgl.h>
#include <stdint.h>

namespace PagerUI
{

/// Colour and metric tokens for the whole UI.
///
/// Styles are applied explicitly from these rather than through lv_theme_default_init(),
/// which costs flash for a look we override anyway.
namespace Theme
{

struct Palette {
    lv_color_t background;
    lv_color_t surface;
    lv_color_t surfaceAlt;
    lv_color_t text;
    lv_color_t textDim;
    lv_color_t divider;
    lv_color_t accent;       // outgoing bubbles, selection fill
    lv_color_t accentBright; // highlights on top of accent
    lv_color_t ok;           // delivered
    lv_color_t pending;      // queued / relayed
    lv_color_t error;        // failed / timed out
    lv_color_t warning;
    lv_color_t secure; // PKI / verified key
};

/// Layout metrics, in pixels, for the 480x222 canvas.
static constexpr int32_t kStatusBarHeight = 22;
static constexpr int32_t kListRowHeight = 44;
static constexpr int32_t kAvatarSize = 36;
static constexpr int32_t kPadding = 8;
static constexpr int32_t kBubbleRadius = 10;
static constexpr int32_t kBubbleMaxWidth = 340;

/// The palette in force. Defaults to dark.
const Palette &palette();

/// Switch palettes and restyle every live object.
void setDark(bool dark);
bool isDark();

/// Stable per-node accent, so a node keeps the same avatar colour across reboots and
/// across screens. Derived from the node number alone - no storage, no allocation.
lv_color_t colorForNode(uint32_t nodeNum);

} // namespace Theme

} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
