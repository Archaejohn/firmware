#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_PAGERUI

#include <LovyanGFX.hpp>

namespace PagerUI
{

// The panel is mounted portrait (222x480) and read out rotated, so the usable canvas is
// landscape. variant.h names the physical dimensions, hence the swap here.
static constexpr int32_t kScreenWidth = TFT_HEIGHT; // 480
static constexpr int32_t kScreenHeight = TFT_WIDTH; // 222

/// LovyanGFX device for the T-Lora Pager's ST7796.
///
/// This duplicates the panel setup in TFTDisplay.cpp:714 on purpose: that one is a
/// file-static class inside a translation unit PagerUI does not compile, so it cannot be
/// reused. Keep the two in sync if the panel wiring ever changes; every value below comes
/// from variants/esp32s3/tlora-pager/variant.h.
class LGFX_TLoraPager : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7796 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;

  public:
    LGFX_TLoraPager()
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host = ST7796_SPI_HOST; // SPI2_HOST
            cfg.spi_mode = 0;
            cfg.freq_write = SPI_FREQUENCY;     // 75 MHz
            cfg.freq_read = SPI_READ_FREQUENCY; // 16 MHz
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = ST7796_SCK;
            cfg.pin_mosi = ST7796_SDA;
            cfg.pin_miso = ST7796_MISO;
            cfg.pin_dc = ST7796_RS;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }

        {
            auto cfg = _panel.config();
            cfg.pin_cs = ST7796_CS;
            cfg.pin_rst = ST7796_RESET;
            cfg.pin_busy = ST7796_BUSY;
            cfg.panel_width = TFT_WIDTH;
            cfg.panel_height = TFT_HEIGHT;
            cfg.offset_x = TFT_OFFSET_X;
            cfg.offset_y = TFT_OFFSET_Y;
            cfg.offset_rotation = TFT_OFFSET_ROTATION;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = true;
            cfg.invert = true;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            // The SX1262 and the SD card sit on this same bus (SCK 35 / MISO 33 / MOSI 34).
            // Without this LovyanGFX assumes it owns the bus and leaves it configured for the
            // panel between transactions.
            cfg.bus_shared = true;
            _panel.config(cfg);
        }

        {
            auto cfg = _light.config();
            cfg.pin_bl = ST7796_BL;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }

        setPanel(&_panel);
    }
};

} // namespace PagerUI

#endif // MESHTASTIC_INCLUDE_PAGERUI
