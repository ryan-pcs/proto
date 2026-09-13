#pragma once
#include <TFT_eSPI.h>
#include "ui_state.h"

struct Button {
    int16_t x, y, w, h;
    const char* label;
    uint16_t colour;
};

bool buttonHit(const Button& b, int16_t tx, int16_t ty);
void drawButton(TFT_eSPI& tft, const Button& b, bool filled);

// Full repaints. Safe between runs, never during an active capture.
void drawBoot(TFT_eSPI& tft, const UiState& s);
void drawHome(TFT_eSPI& tft, const UiState& s);
void drawLabel(TFT_eSPI& tft, const UiState& s);
void drawSettings(TFT_eSPI& tft, const UiState& s);
void drawVerdict(TFT_eSPI& tft, const UiState& s);
void drawMessage(TFT_eSPI& tft, const UiState& s);

// Scanning is split deliberately: the frame is painted once when the run
// starts, and only the small number boxes are repainted while it runs.
// A full repaint mid-capture takes long enough to cost you readings.
void drawScanningFrame(TFT_eSPI& tft, const UiState& s);
void drawScanningLive(TFT_eSPI& tft, const UiState& s);

// --- button layouts, shared between drawing and touch handling ---
extern const Button BTN_HOME_COLLECT;
extern const Button BTN_HOME_IDENTIFY;

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
