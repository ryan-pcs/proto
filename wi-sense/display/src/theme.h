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

// How hard a press has to be before it counts. The graphics library defaults
// to 600, which is too firm for some panels — a normal finger press is simply
// ignored. Lower this if presses are being missed; raise it if the screen
// reacts to presses that never happened.
// How hard a press has to be before it counts. This is only the starting
// value — the sensitivity screen lets you choose one that suits how you
// actually press, and remembers it. Idle readings on this panel sit between
// 0 and about 40, so anything above 50 is clear of the noise.
static const uint16_t TOUCH_THRESHOLD_DEFAULT = 150;

// Set to 1 to turn on touch diagnostics: a "tap anywhere" check screen at
// start-up, and a press reading printed to the serial monitor whenever the
// panel is touched. Set back to 1 if touch ever starts misbehaving.
#define TOUCH_DEBUG 0

// Shown at start-up and on the screen test, so you can tell at a glance which
// build is actually running on the board. Change it whenever the code changes.
#define BUILD_TAG "build-24-sensor"

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
