#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include <stdint.h>

namespace PagerUI
{

/// Binds LVGL to the pager's ST7796 over LovyanGFX.
///
/// Everything here runs on the UI task only - see UiTask.cpp for why that matters.
namespace LvglPort
{

/// Bring up LovyanGFX and LVGL and register the display. Returns false if the draw buffers
/// could not be allocated, in which case the UI must not start.
bool begin();

/// Panel brightness, 0-255. Safe to call from the UI task.
void setBrightness(uint8_t brightness);

/// Blank/unblank for sleep. Takes the SPI lock internally.
void setPowerSave(bool on);

} // namespace LvglPort

} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
