#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include "LvglPort.h"

#include "DebugConfiguration.h"
#include "LGFX_TLoraPager.h"
#include "SPILock.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

namespace PagerUI
{
namespace LvglPort
{

static LGFX_TLoraPager tft;
static lv_display_t *display = nullptr;
static uint8_t *drawBuf[2] = {nullptr, nullptr};
static size_t drawBufBytes = 0;

// Rows of the panel held in each partial draw buffer. Two buffers of this height are
// allocated from internal DMA-capable SRAM: LVGL renders into them with the CPU a byte at a
// time, and the pager's PSRAM is QSPI rather than octal, so rendering there costs several
// times more and DMA out of it needs a bounce buffer. 20 rows is 19.2 kB per buffer.
static constexpr int32_t kPreferredRows = 20;
static constexpr int32_t kMinimumRows = 8;

static void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap)
{
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);

    // LVGL renders RGB565 host-endian; the panel wants big-endian. Swap on the CPU *before*
    // taking the bus, so the LoRa radio is not kept waiting through it.
    lv_draw_sw_rgb565_swap(pxMap, (uint32_t)(w * h));

    {
        // The SX1262 and the SD card share SPI2_HOST with the panel, and spiLock is the
        // arbiter. endWrite() blocks until the DMA transfer has drained, and it must stay
        // inside the lock: releasing the bus with a transfer still in flight lets the radio's
        // next transaction interleave with ours, corrupting one or both.
        //
        // This serialises the UI task against DMA rather than overlapping them. That is
        // deliberate - device-ui's overlapping variant is disabled in its own tree
        // (LGFXDriver.h, the `#if 0` branch) precisely because it signals flush_ready before
        // the transfer completes, which hands the buffer back to LVGL too early.
        concurrency::LockGuard g(spiLock);
        tft.startWrite();
        tft.pushImageDMA(area->x1, area->y1, w, h, (uint16_t *)pxMap);
        tft.endWrite();
    }

    lv_display_flush_ready(disp);
}

static bool allocDrawBuffers()
{
    for (int32_t rows = kPreferredRows; rows >= kMinimumRows; rows /= 2) {
        const size_t bytes = (size_t)kScreenWidth * (size_t)rows * sizeof(uint16_t);
        drawBuf[0] = (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        drawBuf[1] = (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

        if (drawBuf[0] && drawBuf[1]) {
            drawBufBytes = bytes;
            LOG_INFO("PagerUI: draw buffers 2 x %u B (%d rows), internal DMA", (unsigned)bytes, (int)rows);
            return true;
        }

        // Not enough contiguous internal DMA memory at this size. Drop the second buffer
        // before retrying smaller - single-buffered is slower but still correct.
        if (drawBuf[0] && !drawBuf[1]) {
            drawBufBytes = bytes;
            LOG_WARN("PagerUI: only one draw buffer of %u B available; rendering single-buffered", (unsigned)bytes);
            return true;
        }

        heap_caps_free(drawBuf[0]);
        heap_caps_free(drawBuf[1]);
        drawBuf[0] = drawBuf[1] = nullptr;
    }

    LOG_ERROR("PagerUI: no internal DMA memory for draw buffers (largest free block %u B)",
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    return false;
}

bool begin()
{
    {
        concurrency::LockGuard g(spiLock);
        tft.init();
        tft.setRotation(0); // with offset_rotation 3 this yields the 480x222 landscape canvas
        tft.setBrightness(0);
        tft.fillScreen(0x0000);
    }

    if (!allocDrawBuffers())
        return false;

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });

    display = lv_display_create(kScreenWidth, kScreenHeight);
    if (!display) {
        LOG_ERROR("PagerUI: lv_display_create failed");
        return false;
    }
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, flushCb);
    lv_display_set_buffers(display, drawBuf[0], drawBuf[1], drawBufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    LOG_INFO("PagerUI: LVGL up, %dx%d", (int)kScreenWidth, (int)kScreenHeight);
    return true;
}

void setBrightness(uint8_t brightness)
{
    concurrency::LockGuard g(spiLock);
    tft.setBrightness(brightness);
}

void setPowerSave(bool on)
{
    concurrency::LockGuard g(spiLock);
    if (on) {
        tft.setBrightness(0);
        tft.sleep();
        tft.powerSaveOn();
    } else {
        tft.powerSaveOff();
        tft.wakeup();
    }
}

} // namespace LvglPort
} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
