#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include <stdint.h>

namespace PagerUI
{

/// The single task that owns LVGL.
///
/// LVGL is not internally locked in this build (LV_USE_OS is LV_OS_NONE), so the rule that
/// keeps it correct is simple and absolute: **no lv_* call ever runs off this task**. One
/// stray call from the radio or main task is heap corruption that surfaces as an unrelated
/// crash hours later. Anything outside that wants to affect the UI posts a message instead;
/// see InputBridge and the model queue.
namespace UiTask
{

/// Create the task. Call once, after LvglPort::begin() has succeeded.
bool start();

/// True when called from the UI task. Used by assertions at UI entry points.
bool isCurrent();

/// Stop/resume rendering around light sleep.
void suspend();
void resume();

} // namespace UiTask

} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
