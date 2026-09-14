#pragma once
#include "display_driver.h"
#include "ui_state.h"
#include "storage.h"

struct Button {
    int16_t x, y, w, h;
    const char* label;
    uint16_t colour;
};

bool buttonHit(const Button& b, int16_t tx, int16_t ty);
void drawButton(Display& tft, const Button& b, bool filled);

// Full repaints. Safe between runs, never during an active capture.
void drawBoot(Display& tft, const UiState& s);
void drawLabel(Display& tft, const UiState& s);
void drawSettings(Display& tft, const UiState& s);
void drawVerdict(Display& tft, const UiState& s);
void drawMessage(Display& tft, const UiState& s);
void drawTouchSetup(Display& tft, const UiState& s);

// Scanning is split deliberately: the frame is painted once when the run
// starts, and only the small number boxes are repainted while it runs.
// A full repaint mid-capture takes long enough to cost you readings.
void drawScanningFrame(Display& tft, const UiState& s);
void drawScanningLive(Display& tft, const UiState& s);

// --- everyday use at the gate ---
//
// Same split as above wherever something moves: a frame painted once, then a
// small strip refreshed. Full repaints while a scan is running would cost
// readings once the capture is real, so the habit is kept from the start.
void drawReady(Display& tft, const UiState& s);
void drawReadyDistance(Display& tft, const UiState& s);   // small strip only

void drawArmingFrame(Display& tft, const UiState& s);
void drawArmingLive(Display& tft, const UiState& s);      // progress + distance

void drawGateScanFrame(Display& tft, const UiState& s);
void drawGateScanLive(Display& tft, const UiState& s);    // progress + countdown

void drawGateResult(Display& tft, const UiState& s);
void drawGateResultFlash(Display& tft, const UiState& s, bool bright);
void drawGateResultPrompt(Display& tft, const UiState& s);// bottom line only

void drawAdmin(Display& tft, const UiState& s);
void drawStats(Display& tft, const UiState& s, const ScanStats& stats);
void drawReadyTally(Display& tft, const ScanStats& stats);  // small strip only

// --- button layouts, shared between drawing and touch handling ---
extern const Button BTN_LABEL_EMPTY;
extern const Button BTN_LABEL_METAL;
extern const Button BTN_LABEL_NONMETAL;
extern const Button BTN_LABEL_BACK;

extern const Button BTN_SET_5S;
extern const Button BTN_SET_10S;
extern const Button BTN_SET_20S;
extern const Button BTN_SET_20HZ;
extern const Button BTN_SET_50HZ;
extern const Button BTN_SET_BACK;
extern const Button BTN_SET_START;

// Note: there is deliberately no cancel button on the scanning screen. The
// firmware has no cancel path either, so that every started burst is always
// cleaned up the same way.

extern const Button BTN_V_DISCARD;
extern const Button BTN_V_AGAIN;
extern const Button BTN_V_KEEP;

extern const Button BTN_MSG_OK;

extern const Button BTN_TOUCH_START;

// The only two buttons in everyday use, and they are deliberately small and in
// the corners. Nothing has to be pressed to scan a bag. They cannot shrink much
// further: TOUCH_MIN in theme.h is what a fingertip can reliably hit on a
// resistive panel, and these are already at it.
extern const Button BTN_READY_COLLECT;
extern const Button BTN_READY_SETUP;

// The SETUP screen. One row per setting, each with a minus and a plus.
extern const Button BTN_ADM_TRIG_DN;
extern const Button BTN_ADM_TRIG_UP;
extern const Button BTN_ADM_DWELL_DN;
extern const Button BTN_ADM_DWELL_UP;
extern const Button BTN_ADM_SCAN_DN;
extern const Button BTN_ADM_SCAN_UP;
extern const Button BTN_ADM_TOUCH;
extern const Button BTN_ADM_STATS;
extern const Button BTN_ADM_BACK;

extern const Button BTN_STATS_RESET;
extern const Button BTN_STATS_BACK;
