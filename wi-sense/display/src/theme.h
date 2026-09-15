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
#define BUILD_TAG "build-29-cleanstop"

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
#define COL_BAD_DIM 0x6000   // the dark half of the flashing red result

// The coverage threshold that decides whether a run counts, from STORAGE.md.
static const float COVERAGE_MIN = 0.75f;

// ---------------------------------------------------------------------------
// Everyday scanning at the gate.
//
// Starting values only. They are kept in the board's flash once changed on the
// SETUP screen, so these are what a brand new board begins with.
//
// The re-arm gap is the one that is not a matter of taste. Re-arming happens
// further out than triggering on purpose: with a single threshold, a bag left
// sitting near the edge makes the reading cross back and forth and the machine
// scans the same bag over and over. The gap is what stops that.
// ---------------------------------------------------------------------------
static const uint16_t GATE_TRIGGER_MM_DEFAULT = 300;
static const uint16_t GATE_REARM_GAP_MM       = 150;
static const uint16_t GATE_DWELL_MS_DEFAULT   = 600;
static const uint16_t GATE_SCAN_MS_DEFAULT    = 2500;

// The two thresholds that turn a detector's confidence (0-100) into one of the
// three levels. Below the first is green, above the second is red, and between
// them is the orange band where the machine says it is not sure.
//
// These numbers are placeholders. They cannot be chosen properly until a
// detector exists and has been tested against bags whose contents are known -
// where they sit is the trade between waving through a bag with metal in it
// and hand-searching bags that had none. That is a decision about how the
// school wants to run the gate, not a number to guess at a desk.
static const uint8_t GATE_CONF_UNSURE = 40;   // green below this
static const uint8_t GATE_CONF_METAL  = 75;   // red at or above this

// How fast the red result flashes. Kept at roughly 1.7 flashes a second: fast
// enough to read as an alarm, and deliberately below the three-a-second mark
// that is the usual guidance for anything that might be looked at by someone
// with photosensitive epilepsy.
static const uint16_t GATE_FLASH_MS = 300;

// What the SETUP screen will let the three settings above be changed to, and
// by how much one press moves them.
static const uint16_t GATE_TRIGGER_MM_MIN  = 100,  GATE_TRIGGER_MM_MAX  = 700,  GATE_TRIGGER_MM_STEP = 25;
static const uint16_t GATE_DWELL_MS_MIN    = 0,    GATE_DWELL_MS_MAX    = 2000, GATE_DWELL_MS_STEP   = 100;
static const uint16_t GATE_SCAN_MS_MIN     = 1000, GATE_SCAN_MS_MAX     = 6000, GATE_SCAN_MS_STEP    = 500;
