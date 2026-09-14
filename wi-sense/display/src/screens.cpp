#include <string.h>
#include <stdio.h>
#include "screens.h"
#include "theme.h"

// ---------------------------------------------------------------------------
// Button layouts. Defined once here so the drawing code and the touch code
// can never disagree about where a button is.
// ---------------------------------------------------------------------------
const Button BTN_HOME_COLLECT  = {  30,  90, 200, 110, "COLLECT",     COL_ACCENT };
const Button BTN_HOME_IDENTIFY = { 250,  90, 200, 110, "IDENTIFY",    COL_MUTED  };
const Button BTN_HOME_SENS     = { 165, 246, 150,  46, "TOUCH SETUP", COL_MUTED  };

const Button BTN_LABEL_EMPTY    = {  20,  90, 140, 120, "EMPTY",     COL_ACCENT };
const Button BTN_LABEL_METAL    = { 170,  90, 140, 120, "METAL",     COL_ACCENT };
const Button BTN_LABEL_NONMETAL = { 320,  90, 140, 120, "NON-METAL", COL_ACCENT };
const Button BTN_LABEL_BACK     = {  20, 260, 140,  56, "BACK",      COL_MUTED  };

const Button BTN_SET_5S    = {  30,  70, 130, 56, "5s",    COL_ACCENT };
const Button BTN_SET_10S   = { 175,  70, 130, 56, "10s",   COL_ACCENT };
const Button BTN_SET_20S   = { 320,  70, 130, 56, "20s",   COL_ACCENT };
const Button BTN_SET_20HZ  = {  30, 155, 200, 56, "20 Hz", COL_ACCENT };
const Button BTN_SET_50HZ  = { 250, 155, 200, 56, "50 Hz", COL_ACCENT };
const Button BTN_SET_BACK  = {  20, 260, 140, 56, "BACK",  COL_MUTED  };
const Button BTN_SET_START = { 300, 260, 160, 56, "START", COL_OK     };

const Button BTN_V_DISCARD = {  20, 260, 140, 56, "DISCARD", COL_BAD    };
const Button BTN_V_AGAIN   = { 170, 260, 140, 56, "AGAIN",   COL_MUTED  };
const Button BTN_V_KEEP    = { 320, 260, 140, 56, "KEEP",    COL_OK     };

const Button BTN_MSG_OK    = { 170, 260, 140, 56, "OK",      COL_ACCENT };

const Button BTN_TOUCH_START = { 165, 246, 150, 56, "START", COL_ACCENT };

// Stat box geometry on the scanning screen, shared by the full paint and the
// live update so they line up exactly.
static const int16_t STAT_Y = 130;
static const int16_t STAT_W = 105;
static const int16_t STAT_H = 100;
static const int16_t STAT_X[4] = { 15, 130, 245, 360 };

static int16_t lastProgressW = 0;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static void text(Display& tft, const char* s, int16_t x, int16_t y,
                 uint8_t font, uint8_t size, uint16_t fg, uint16_t bg, uint8_t datum) {
    tft.setTextDatum(datum);
    tft.setTextColor(fg, bg);
    tft.setTextSize(size);
    tft.drawString(s, x, y, font);
    tft.setTextSize(1);
}

bool buttonHit(const Button& b, int16_t tx, int16_t ty) {
    return tx >= b.x && tx < (b.x + b.w) && ty >= b.y && ty < (b.y + b.h);
}

void drawButton(Display& tft, const Button& b, bool filled) {
    // Long labels drop to the smaller font so they still fit the box.
    uint8_t font = (strlen(b.label) > 8) ? 2 : 4;

    if (filled) {
        tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, b.colour);
        text(tft, b.label, b.x + b.w / 2, b.y + b.h / 2, font, 1, COL_BG, b.colour, MC_DATUM);
    } else {
        tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, COL_PANEL);
        tft.drawRoundRect(b.x, b.y, b.w, b.h, 8, b.colour);
        text(tft, b.label, b.x + b.w / 2, b.y + b.h / 2, font, 1, b.colour, COL_PANEL, MC_DATUM);
    }
}

static void drawStatusBar(Display& tft, const UiState& s, const char* mode) {
    char buf[32];
    tft.fillRect(0, 0, SCREEN_W, BAR_TOP_H, COL_PANEL);

    snprintf(buf, sizeof(buf), "LINK %s", s.wifiLinked ? "OK" : "--");
    text(tft, buf, 12, BAR_TOP_H / 2, 2, 1,
         s.wifiLinked ? COL_OK : COL_BAD, COL_PANEL, ML_DATUM);

    text(tft, mode, SCREEN_W / 2, BAR_TOP_H / 2, 2, 1, COL_TEXT, COL_PANEL, MC_DATUM);

    snprintf(buf, sizeof(buf), "CARD %s", s.cardPresent ? "OK" : "--");
    text(tft, buf, SCREEN_W - 12, BAR_TOP_H / 2, 2, 1,
         s.cardPresent ? COL_OK : COL_MUTED, COL_PANEL, MR_DATUM);
}

static void drawCheckLine(Display& tft, int16_t y, const char* label, bool ok) {
    text(tft, label, 60, y, 4, 1, COL_TEXT, COL_BG, ML_DATUM);
    text(tft, ok ? "OK" : "....", 420, y, 4, 1,
         ok ? COL_OK : COL_MUTED, COL_BG, MR_DATUM);
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------
void drawBoot(Display& tft, const UiState& s) {
    tft.fillScreen(COL_BG);
    text(tft, "Wi-Sense", SCREEN_W / 2, 38, 4, 1, COL_ACCENT, COL_BG, MC_DATUM);
    text(tft, "lights should flash red, then green, then blue",
         SCREEN_W / 2, 66, 2, 1, COL_MUTED, COL_BG, MC_DATUM);

    // The last two are real checks. The first three are still invented, and
    // become real as the receiver firmware and the transmitter are joined on.
    drawCheckLine(tft, 100, "Receiver",     s.receiverReady);
    drawCheckLine(tft, 134, "WiFi link",    s.wifiLinked);
    drawCheckLine(tft, 168, "Transmitter",  s.transmitterReady);
    drawCheckLine(tft, 202, "Card",         s.cardPresent);
    drawCheckLine(tft, 236, "Distance",     s.sensorPresent);

    if (s.cardDetail[0] != '\0') {
        text(tft, s.cardDetail, SCREEN_W / 2, 265, 2, 1,
             s.cardWorking ? COL_MUTED : COL_WARN, COL_BG, MC_DATUM);
    }
    if (s.sensorDetail[0] != '\0') {
        text(tft, s.sensorDetail, SCREEN_W / 2, 285, 2, 1,
             s.sensorWorking ? COL_MUTED : COL_WARN, COL_BG, MC_DATUM);
    }

    text(tft, BUILD_TAG, SCREEN_W / 2, 308, 2, 1, COL_MUTED, COL_BG, MC_DATUM);
}

void drawHome(Display& tft, const UiState& s) {
    tft.fillScreen(COL_BG);
    drawStatusBar(tft, s, "READY");

    drawHomeDistance(tft, s);

    drawButton(tft, BTN_HOME_COLLECT, true);
    drawButton(tft, BTN_HOME_IDENTIFY, false);

    text(tft, s.modelTrained ? "detector loaded" : "no detector trained yet",
         SCREEN_W / 2, 222, 2, 1,
         s.modelTrained ? COL_OK : COL_WARN, COL_BG, MC_DATUM);

    drawButton(tft, BTN_HOME_SENS, false);

    // Which build is actually on the board. Kept on the home screen so it can
    // be checked at any time rather than caught during start-up.
    text(tft, BUILD_TAG, SCREEN_W / 2, 308, 2, 1, COL_MUTED, COL_BG, MC_DATUM);
}

// Refreshed several times a second on the home screen, so you can wave a hand
// in front of the sensor and watch the number follow it. Repaints only its own
// strip, never the whole screen.
void drawHomeDistance(Display& tft, const UiState& s) {
    char buf[40];

    tft.fillRect(90, 52, 300, 26, COL_BG);

    if (!s.sensorWorking) {
        snprintf(buf, sizeof(buf), "distance sensor not found");
        text(tft, buf, SCREEN_W / 2, 64, 2, 1, COL_WARN, COL_BG, MC_DATUM);
    } else if (s.sensorInRange) {
        snprintf(buf, sizeof(buf), "distance   %u mm", s.distanceMm);
        text(tft, buf, SCREEN_W / 2, 64, 2, 1, COL_OK, COL_BG, MC_DATUM);
    } else {
        snprintf(buf, sizeof(buf), "distance   nothing in range");
        text(tft, buf, SCREEN_W / 2, 64, 2, 1, COL_MUTED, COL_BG, MC_DATUM);
    }
}

void drawLabel(Display& tft, const UiState& s) {
    tft.fillScreen(COL_BG);
    drawStatusBar(tft, s, "COLLECT");

    text(tft, "What is in the tray?", SCREEN_W / 2, 62, 4, 1, COL_TEXT, COL_BG, MC_DATUM);

    drawButton(tft, BTN_LABEL_EMPTY,    s.category == CAT_EMPTY);
    drawButton(tft, BTN_LABEL_METAL,    s.category == CAT_METAL);
    drawButton(tft, BTN_LABEL_NONMETAL, s.category == CAT_NON_METAL);

    drawButton(tft, BTN_LABEL_BACK, false);
}

void drawSettings(Display& tft, const UiState& s) {
    char buf[72];
    tft.fillScreen(COL_BG);
    drawStatusBar(tft, s, "COLLECT");

    text(tft, "DURATION", 30, 56, 2, 1, COL_MUTED, COL_BG, ML_DATUM);
    drawButton(tft, BTN_SET_5S,  s.durationS == 5);
    drawButton(tft, BTN_SET_10S, s.durationS == 10);
    drawButton(tft, BTN_SET_20S, s.durationS == 20);

    text(tft, "RATE", 30, 141, 2, 1, COL_MUTED, COL_BG, ML_DATUM);
    drawButton(tft, BTN_SET_20HZ, s.rateHz == 20);
    drawButton(tft, BTN_SET_50HZ, s.rateHz == 50);

    snprintf(buf, sizeof(buf), "data\\%s\\%s_%s_%us_%uhz",
             categoryFolder(s.category), s.objectName,
             categoryFolder(s.category), s.durationS, s.rateHz);
    text(tft, buf, SCREEN_W / 2, 234, 2, 1, COL_MUTED, COL_BG, MC_DATUM);

    drawButton(tft, BTN_SET_BACK, false);
    drawButton(tft, BTN_SET_START, true);
}

// Painted once, when the run begins.
void drawScanningFrame(Display& tft, const UiState& s) {
    char buf[40];
    tft.fillScreen(COL_BG);

    snprintf(buf, sizeof(buf), "SCANNING - %s", categoryName(s.category));
    drawStatusBar(tft, s, buf);

    // progress bar outline
    tft.drawRoundRect(20, 55, 440, 28, 6, COL_MUTED);
    lastProgressW = 0;

    const char* labels[4] = { "SENT", "ROWS", "DROPS", "COVERAGE" };
    for (int i = 0; i < 4; i++) {
        tft.fillRoundRect(STAT_X[i], STAT_Y, STAT_W, STAT_H, 6, COL_PANEL);
        text(tft, labels[i], STAT_X[i] + STAT_W / 2, STAT_Y + 20, 2, 1,
             COL_MUTED, COL_PANEL, MC_DATUM);
    }

    // No cancel button, on purpose. The firmware deliberately has no cancel
    // path so that every started burst is cleaned up the same way.
    text(tft, "run finishes on its own", SCREEN_W / 2, BAR_BOT_Y + 28, 2, 1,
         COL_MUTED, COL_BG, MC_DATUM);
}

// Called repeatedly while the run is active. Repaints only small regions.
void drawScanningLive(Display& tft, const UiState& s) {
    char buf[16];

    // --- progress bar: draw only the newly filled slice ---
    uint32_t totalMs = (uint32_t)s.durationS * 1000UL;
    int16_t w = 0;
    if (totalMs > 0) {
        uint32_t done = s.elapsedMs > totalMs ? totalMs : s.elapsedMs;
        w = (int16_t)((uint32_t)436 * done / totalMs);
    }
    if (w > lastProgressW) {
        tft.fillRect(22 + lastProgressW, 57, w - lastProgressW, 24, COL_ACCENT);
        lastProgressW = w;
    }

    // --- seconds remaining ---
    uint32_t remainMs = (s.elapsedMs >= totalMs) ? 0 : (totalMs - s.elapsedMs);
    snprintf(buf, sizeof(buf), "%lus left", (unsigned long)((remainMs + 999) / 1000));
    tft.fillRect(140, 95, 200, 26, COL_BG);
    text(tft, buf, SCREEN_W / 2, 108, 4, 1, COL_TEXT, COL_BG, MC_DATUM);

    // --- the four numbers ---
    const int16_t vy = STAT_Y + STAT_H / 2 + 14;

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)s.packetsSent);
    tft.fillRect(STAT_X[0] + 4, vy - 16, STAT_W - 8, 32, COL_PANEL);
    text(tft, buf, STAT_X[0] + STAT_W / 2, vy, 4, 1, COL_TEXT, COL_PANEL, MC_DATUM);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)s.rowsCaptured);
    tft.fillRect(STAT_X[1] + 4, vy - 16, STAT_W - 8, 32, COL_PANEL);
    text(tft, buf, STAT_X[1] + STAT_W / 2, vy, 4, 1, COL_TEXT, COL_PANEL, MC_DATUM);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)s.drops);
    tft.fillRect(STAT_X[2] + 4, vy - 16, STAT_W - 8, 32, COL_PANEL);
    text(tft, buf, STAT_X[2] + STAT_W / 2, vy, 4, 1,
         s.drops > 0 ? COL_BAD : COL_TEXT, COL_PANEL, MC_DATUM);

    snprintf(buf, sizeof(buf), "%d%%", (int)(s.coverage * 100.0f));
    tft.fillRect(STAT_X[3] + 4, vy - 16, STAT_W - 8, 32, COL_PANEL);
    text(tft, buf, STAT_X[3] + STAT_W / 2, vy, 4, 1,
         (s.coverage >= COVERAGE_MIN) ? COL_OK : COL_BAD, COL_PANEL, MC_DATUM);
}

void drawVerdict(Display& tft, const UiState& s) {
    char buf[72];
    tft.fillScreen(COL_BG);
    drawStatusBar(tft, s, "RESULT");

    text(tft, s.runPassed ? "PASS" : "FAIL", SCREEN_W / 2, 100, 4, 3,
         s.runPassed ? COL_OK : COL_BAD, COL_BG, MC_DATUM);

    snprintf(buf, sizeof(buf), "%s / %s  %us  %uhz",
             categoryFolder(s.category), s.objectName, s.durationS, s.rateHz);
    text(tft, buf, SCREEN_W / 2, 165, 2, 1, COL_MUTED, COL_BG, MC_DATUM);

    if (s.runPassed) {
        snprintf(buf, sizeof(buf), "coverage %d%%, no drops",
                 (int)(s.coverage * 100.0f));
        text(tft, buf, SCREEN_W / 2, 200, 4, 1, COL_TEXT, COL_BG, MC_DATUM);
    } else {
        text(tft, s.failReason, SCREEN_W / 2, 200, 4, 1, COL_WARN, COL_BG, MC_DATUM);
    }

    drawButton(tft, BTN_V_DISCARD, false);
    drawButton(tft, BTN_V_AGAIN,   false);
    drawButton(tft, BTN_V_KEEP,    s.runPassed);
}

void drawMessage(Display& tft, const UiState& s) {
    tft.fillScreen(COL_BG);
    drawStatusBar(tft, s, "NOTICE");

    text(tft, s.messageTitle, SCREEN_W / 2, 110, 4, 2, COL_WARN, COL_BG, MC_DATUM);
    text(tft, s.messageBody,  SCREEN_W / 2, 175, 2, 1, COL_TEXT, COL_BG, MC_DATUM);

    drawButton(tft, BTN_MSG_OK, false);
}

// The actual calibration (the moving target markers) is drawn by the graphics
// library itself, from inside calibrateTouch() in main.cpp — this screen is
// only the instruction before that starts.
void drawTouchSetup(Display& tft, const UiState& s) {
    (void)s;
    tft.fillScreen(COL_BG);
    drawStatusBar(tft, s, "TOUCH SETUP");

    text(tft, "Touch calibration", SCREEN_W / 2, 90, 4, 1, COL_TEXT, COL_BG, MC_DATUM);
    text(tft, "Press START, then touch each",
         SCREEN_W / 2, 140, 2, 1, COL_MUTED, COL_BG, MC_DATUM);
    text(tft, "marker with the stylus as it appears.",
         SCREEN_W / 2, 164, 2, 1, COL_MUTED, COL_BG, MC_DATUM);
    text(tft, "Saved on the chip - survives power off.",
         SCREEN_W / 2, 196, 2, 1, COL_MUTED, COL_BG, MC_DATUM);

    drawButton(tft, BTN_TOUCH_START, true);
}
