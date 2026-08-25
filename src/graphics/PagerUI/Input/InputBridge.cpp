#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include "InputBridge.h"

#include "DebugConfiguration.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace PagerUI
{

InputBridge inputBridge;

// Deep enough to absorb a fast wheel spin between UI frames (~30 ms), shallow enough that a
// stalled UI task drops stale input rather than replaying a backlog when it recovers.
static constexpr UBaseType_t kQueueDepth = 16;

static QueueHandle_t queue = nullptr;

bool InputBridge::begin()
{
    if (!queue) {
        queue = xQueueCreate(kQueueDepth, sizeof(InputEvent));
        if (!queue) {
            LOG_ERROR("PagerUI: could not create input queue");
            return false;
        }
    }

    if (!inputBroker) {
        LOG_ERROR("PagerUI: no InputBroker - keyboard and wheel will not work");
        return false;
    }

    observer.observe(inputBroker);
    return true;
}

int InputBridge::onEvent(const InputEvent *event)
{
    if (event && queue) {
        // Never block: this can run on the input-poll task, and stalling it would stall the
        // encoder. A full queue means the UI is behind, and the oldest keystroke is the one
        // worth losing.
        if (xQueueSend(queue, event, 0) != pdTRUE) {
            InputEvent discarded;
            if (xQueueReceive(queue, &discarded, 0) == pdTRUE)
                (void)xQueueSend(queue, event, 0);
        }
    }

    // Observable::notifyObservers stops at the first non-zero return, so returning anything
    // else here would swallow the event before ExternalNotificationModule and the other
    // InputBroker consumers ever see it.
    return 0;
}

bool InputBridge::poll(InputEvent &out)
{
    return queue && xQueueReceive(queue, &out, 0) == pdTRUE;
}

} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
