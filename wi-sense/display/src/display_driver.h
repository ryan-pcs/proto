#pragma once

// ---------------------------------------------------------------------------
// The screen.
//
// The panel is described here in code rather than through build settings.
// That is the main practical difference from the library we used before, and
// the reason this one works on the S3: nothing is guessed at build time.
//
// Pins for both boards live in board_pins.h, so this file does not need to
// know which chip it is running on.
// ---------------------------------------------------------------------------

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "board_pins.h"

class WiSenseScreen : public lgfx::LGFX_Device {
    lgfx::Panel_ST7796  _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Touch_XPT2046 _touch;

public:
    WiSenseScreen() {
        {
            auto cfg = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 20000000;
            cfg.freq_read   =  8000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = PIN_TFT_SCLK;
            cfg.pin_mosi    = PIN_TFT_MOSI;
            cfg.pin_miso    = PIN_TFT_MISO;
            cfg.pin_dc      = PIN_TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs       = PIN_TFT_CS;
            cfg.pin_rst      = PIN_TFT_RST;
            cfg.pin_busy     = -1;
            cfg.panel_width  = 320;
            cfg.panel_height = 480;
            cfg.offset_x     = 0;
            cfg.offset_y     = 0;
            cfg.readable     = true;
            cfg.invert       = false;
            cfg.rgb_order    = false;
            cfg.dlen_16bit   = false;

            // The memory card shares these wires, so the library must put the
            // bus back the way it found it around each of its own transfers.
            cfg.bus_shared   = true;

            _panel.config(cfg);
        }
        {
            // Touch shares the same three wires as the screen (SCK/MOSI/MISO),
            // with its own select line, T_CS. T_IRQ is not wired on the bench
            // board, so this polls for a touch rather than waiting for an
            // interrupt (pin_int = -1) — normal for this chip.
            auto cfg = _touch.config();
            cfg.x_min          = 0;
            cfg.x_max          = 4095;
            cfg.y_min          = 0;
            cfg.y_max          = 4095;
            cfg.pin_int        = -1;
            cfg.bus_shared     = true;
            cfg.offset_rotation = 0;
            cfg.spi_host       = SPI2_HOST;
            cfg.freq           = 1000000;
            cfg.pin_sclk       = PIN_TFT_SCLK;
            cfg.pin_mosi       = PIN_TFT_MOSI;
            cfg.pin_miso       = PIN_TFT_MISO;
            cfg.pin_cs         = PIN_TOUCH_CS;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

using Display = WiSenseScreen;
