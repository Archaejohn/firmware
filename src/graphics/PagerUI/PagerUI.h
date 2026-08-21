#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

namespace PagerUI
{

/// Adjust persisted config that must be correct *before* InputBroker::Init() constructs the
/// input drivers. Call right after NodeDB is constructed (config is loaded in its ctor) and
/// well before setupModules(). Safe to call more than once.
void preInit();

/// Bring up the UI: display, LVGL, the render task, and the model observers.
/// Call after setupModules(), so the UI can observe modules the way InkHUD does.
void setup();

} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
