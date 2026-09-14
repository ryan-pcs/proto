#pragma once

// ---------------------------------------------------------------------------
// Every pin, for both boards, in one place.
//
// The screen's pins are here too now. They used to live in platformio.ini
// because the old graphics library read them at build time; the one we use
// now is told them in code, which is both clearer and one less place to look.
//
// Two boards are supported, chosen by the build profile:
//   esp32s3  - ESP32-S3 N16R8
//   esp32dev - classic ESP32-WROOM-32
//
// The two chips do not share a pin numbering scheme. On the S3 several of the
// classic board's numbers do not exist at all, and others are reserved for the
// chip's own memory. Hence two tables.
// ---------------------------------------------------------------------------

#ifdef BOARD_S3

// ---- ESP32-S3 N16R8 ----
static const int PIN_TFT_SCLK  = 12;
static const int PIN_TFT_MOSI  = 11;
static const int PIN_TFT_MISO  = 13;
static const int PIN_TFT_CS    = 10;
static const int PIN_TFT_DC    = 14;
static const int PIN_TFT_RST   =  9;
static const int PIN_TOUCH_CS  = 15;

static const int PIN_BACKLIGHT =  8;
static const int PIN_LED_GREEN =  4;
static const int PIN_LED_RED   =  5;
static const int PIN_LED_BLUE  =  6;
static const int PIN_TOF_SDA   = 17;
static const int PIN_TOF_SCL   = 18;

// The back-panel microSD slot on the *display*, shared with the screen's
// wires. Not currently used - see PIN_SD_CLK etc. below instead, for the
// card reader built into this board itself.
static const int PIN_SD_CS     = 16;

// This specific S3 board has its own onboard card reader, wired separately
// from the display's slot - own dedicated pins, no wire-sharing with the
// screen. 1-wire SD/MMC mode: three pins, confirmed against this board's
// silkscreen/documentation rather than assumed.
static const int PIN_SD_CLK    = 39;
static const int PIN_SD_CMD    = 38;
static const int PIN_SD_D0     = 40;

#else

// ---- classic ESP32-WROOM-32 ----
static const int PIN_TFT_SCLK  = 18;
static const int PIN_TFT_MOSI  = 23;
static const int PIN_TFT_MISO  = 19;
static const int PIN_TFT_CS    = 33;
static const int PIN_TFT_DC    = 25;
static const int PIN_TFT_RST   = 26;
static const int PIN_TOUCH_CS  =  4;

static const int PIN_BACKLIGHT = 32;
static const int PIN_LED_GREEN = 27;
static const int PIN_LED_RED   = 14;
static const int PIN_LED_BLUE  = 13;
static const int PIN_TOF_SDA   = 21;
static const int PIN_TOF_SCL   = 22;

// The back-panel microSD slot on the display, shared with the screen's
// wires. This is the only card slot available on the classic board - it has
// no onboard reader of its own, unlike the S3 bench board above.
static const int PIN_SD_CS     = 17;

#endif
