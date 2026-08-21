#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include "UiTask.h"

#include "DebugConfiguration.h"
#include "Input/InputBridge.h"
#include "PagerUI.h"
#include <Arduino.h>
#include <lvgl.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace PagerUI
{
namespace UiTask
{

// device-ui runs its equivalent task with 16 kB, but that budget also covers PNG decoding and
// map tile work this UI does not do. Start here and trim once uxTaskGetStackHighWaterMark has
// something to say on real hardware.
static constexpr uint32_t kStackBytes = 12288;

// Core 0 alongside the radio-adjacent work, leaving core 1 for the Arduino loop, and priority
// 1 so the UI never preempts the LoRa stack.
static constexpr UBaseType_t kPriority = 1;
static constexpr BaseType_t kCore = 0;

// LVGL asks to be called back at a specific time; clamp it so we neither spin nor sleep
// through an animation frame.
static constexpr uint32_t kMinDelayMs = 5;
static constexpr uint32_t kMaxDelayMs = 30;

static TaskHandle_t taskHandle = nullptr;

bool isCurrent()
{
    return taskHandle != nullptr && xTaskGetCurrentTaskHandle() == taskHandle;
}

static void taskLoop(void *)
{
    for (;;) {
        // Drain input before rendering, so a keystroke is reflected in the frame it caused
        // rather than the one after. This is the boundary where lv_* calls become legal.
        InputEvent ev;
        while (inputBridge.poll(ev))
            handleInputEvent(ev);

        uint32_t next = lv_timer_handler();
        if (next < kMinDelayMs)
            next = kMinDelayMs;
        else if (next > kMaxDelayMs)
            next = kMaxDelayMs;
        vTaskDelay(pdMS_TO_TICKS(next));
    }
}

bool start()
{
    if (taskHandle) {
        LOG_WARN("PagerUI: UI task already running");
        return true;
    }

    const BaseType_t ok = xTaskCreatePinnedToCore(taskLoop, "pagerui", kStackBytes, nullptr, kPriority, &taskHandle, kCore);
    if (ok != pdPASS) {
        taskHandle = nullptr;
        LOG_ERROR("PagerUI: could not create UI task");
        return false;
    }

    LOG_INFO("PagerUI: UI task started (core %d, %u B stack)", (int)kCore, (unsigned)kStackBytes);
    return true;
}

void suspend()
{
    if (taskHandle)
        vTaskSuspend(taskHandle);
}

void resume()
{
    if (taskHandle)
        vTaskResume(taskHandle);
}

} // namespace UiTask
} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
