// ---------------------------------------------------------------------------
// Screen test
//
// One question, nothing else: does this panel work on this chip?
//
// A different graphics library from the main project, a hand-written panel
// description rather than build-time settings, and no card, touch, sensor or
// lights. Colours, forever.
//
// Wiring (ESP32-S3):
//   VCC  -> 5V        SCK  -> IO12      CS   -> IO10
//   GND  -> GND       SDI  -> IO11      DC   -> IO14
//   LED  -> 3.3V      SDO  -> IO13      RST  -> IO9
// ---------------------------------------------------------------------------

#define LGFX_USE_V1
#include <Arduino.h>
#include <LovyanGFX.hpp>

class Screen : public lgfx::LGFX_Device {
    lgfx::Panel_ST7796 _panel;
    lgfx::Bus_SPI      _bus;

public:
    Screen() {
        {
            auto cfg = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 20000000;   // deliberately modest
            cfg.freq_read   =  8000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = 12;
            cfg.pin_mosi    = 11;
            cfg.pin_miso    = 13;
            cfg.pin_dc      = 14;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs        = 10;
            cfg.pin_rst       =  9;
            cfg.pin_busy      = -1;
            cfg.panel_width   = 320;
            cfg.panel_height  = 480;
            cfg.offset_x      = 0;
            cfg.offset_y      = 0;
            cfg.readable      = true;
            cfg.invert        = false;
            cfg.rgb_order     = false;
            cfg.dlen_16bit    = false;
            cfg.bus_shared    = false;
            _panel.config(cfg);
        }
        setPanel(&_panel);
    }
};

static Screen screen;

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println();
    Serial.println("=== screen test - LovyanGFX ===");
    Serial.println("pins: SCLK=12 MOSI=11 MISO=13 CS=10 DC=14 RST=9");

    bool ok = screen.init();
    Serial.print("init returned: ");
    Serial.println(ok ? "true" : "false");

    screen.setRotation(1);      // landscape, 480 x 320
    Serial.print("size after rotation: ");
    Serial.print(screen.width());
    Serial.print(" x ");
    Serial.println(screen.height());
}

void loop() {
    static uint32_t pass = 0;
    pass++;

    Serial.print("pass ");
    Serial.print(pass);
    Serial.println("  - red, green, blue, then the pattern");

    screen.fillScreen(TFT_RED);    delay(700);
    screen.fillScreen(TFT_GREEN);  delay(700);
    screen.fillScreen(TFT_BLUE);   delay(700);

    screen.fillScreen(TFT_BLACK);
    screen.fillRect(0, 0, 40, 40, TFT_CYAN);
    screen.fillRect(screen.width() - 40, 0, 40, 40, TFT_CYAN);
    screen.fillRect(0, screen.height() - 40, 40, 40, TFT_CYAN);
    screen.fillRect(screen.width() - 40, screen.height() - 40, 40, 40, TFT_CYAN);

    screen.setTextDatum(middle_center);
    screen.setTextColor(TFT_WHITE, TFT_BLACK);
    screen.setTextSize(2);
    screen.drawString("SCREEN OK", screen.width() / 2, screen.height() / 2);
    screen.setTextSize(1);

    delay(2000);
}
