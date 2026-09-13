#pragma once

// ---------------------------------------------------------------------------
// Pins that this program drives itself.
//
// The screen's own pins are NOT here — the graphics library needs them at
// build time, so they live in platformio.ini, one set per board.
//
// Two boards are supported, and the choice is made by which build profile you
// use. The program code is identical for both.
//
//   esp32s3  - ESP32-S3 N16R8, the bench board used to develop the screens
//   esp32dev - classic ESP32-WROOM-32, the board that becomes the receiver
//
// The two chips do not share a pin numbering scheme. On the S3, several of the
// classic board's numbers do not exist at all, and others are reserved for the
// chip's own memory. Hence two tables.
// ---------------------------------------------------------------------------

#ifdef BOARD_S3

// ---- ESP32-S3 N16R8 (bench board) ----
static const int PIN_BACKLIGHT = 8;
static const int PIN_LED_GREEN = 4;
static const int PIN_LED_RED   = 5;
static const int PIN_LED_BLUE  = 6;
static const int PIN_TOF_SDA   = 17;
static const int PIN_TOF_SCL   = 18;

#else

// ---- classic ESP32-WROOM-32 (final receiver) ----
static const int PIN_BACKLIGHT = 32;
static const int PIN_LED_GREEN = 27;
static const int PIN_LED_RED   = 14;
static const int PIN_LED_BLUE  = 13;
static const int PIN_TOF_SDA   = 21;
static const int PIN_TOF_SCL   = 22;

#endif
