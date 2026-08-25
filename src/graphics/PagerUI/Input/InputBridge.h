#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include "Observer.h"
#include "input/InputBroker.h"

namespace PagerUI
{

/// Carries InputBroker events onto the UI task.
///
/// The broker notifies its observers on at least two contexts - the main loop, via
/// InputBroker::processInputEventQueue(), and the dedicated `input-pollSoon` task where
/// RotaryEncoderImpl::pollOnce() runs - and never on the UI task. Since LVGL is not internally
/// locked in this build, the observer callback must not touch a single lv_* symbol: it copies
/// the event into a queue and returns immediately.
///
/// InputEvent is a small POD whose only pointer member (`source`) always points at a string
/// literal, so copying it by value across the queue is safe.
class InputBridge
{
  public:
    /// Subscribe to the broker. Safe to call before the UI task exists.
    bool begin();

    /// Pop the next event, or return false if none is waiting. UI task only.
    bool poll(InputEvent &out);

  private:
    int onEvent(const InputEvent *event);

    CallbackObserver<InputBridge, const InputEvent *> observer =
        CallbackObserver<InputBridge, const InputEvent *>(this, &InputBridge::onEvent);
};

/// The bridge feeding the UI task.
extern InputBridge inputBridge;

} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
