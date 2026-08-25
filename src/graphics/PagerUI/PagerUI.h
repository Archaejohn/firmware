#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

struct _InputEvent;

namespace PagerUI
{

/// Adjust persisted config that must be correct *before* InputBroker::Init() constructs the
/// input drivers. Call right after NodeDB is constructed (config is loaded in its ctor) and
/// well before setupModules(). Safe to call more than once.
void preInit();

/// Handle one input event. Called by the UI task after draining the bridge queue, so this is
/// the first point at which touching LVGL is legal.
void handleInputEvent(const ::_InputEvent &event);

/// Bring up the UI: display, LVGL, the render task, and the model observers.
/// Call after setupModules(), so the UI can observe modules the way InkHUD does.
void setup();

} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
