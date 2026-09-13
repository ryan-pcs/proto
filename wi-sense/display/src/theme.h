#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// Screen geometry. The panel is 480 wide by 320 tall in landscape.
// Every screen uses the same three bands so the layout feels consistent.
// ---------------------------------------------------------------------------
static const int16_t SCREEN_W = 480;
static const int16_t SCREEN_H = 320;

static const int16_t BAR_TOP_H = 40;   // status strip
static const int16_t BAR_BOT_H = 64;   // action buttons

static const int16_t CONTENT_Y = BAR_TOP_H;                        // 40
static const int16_t CONTENT_H = SCREEN_H - BAR_TOP_H - BAR_BOT_H; // 216
static const int16_t BAR_BOT_Y = SCREEN_H - BAR_BOT_H;             // 256

// Resistive touch pressed with a fingertip is imprecise. Nothing you must
// hit should be smaller than this.
static const int16_t TOUCH_MIN = 60;

// ---------------------------------------------------------------------------
// Colours, in the screen's 16-bit format.
// ---------------------------------------------------------------------------
#define COL_BG      0x1082   // near-black background
#define COL_PANEL   0x2124   // slightly lighter card
#define COL_TEXT    0xFFFF   // white
#define COL_MUTED   0x8410   // grey, for labels
#define COL_ACCENT  0x05BF   // bright blue, for the primary action
#define COL_OK      0x0660   // green
#define COL_WARN    0xFD20   // amber
#define COL_BAD     0xF800   // red

// The coverage threshold that decides whether a run counts, from STORAGE.md.
static const float COVERAGE_MIN = 0.75f;
