#include <string.h>
#include <stdio.h>
#include "screens.h"
#include "theme.h"

// ---------------------------------------------------------------------------
// Button layouts. Defined once here so the drawing code and the touch code
// can never disagree about where a button is.
// ---------------------------------------------------------------------------
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

const Button BTN_READY_COLLECT = {  14, 248, 104, 60, "COLLECT", COL_MUTED };
const Button BTN_READY_SETUP   = { 362, 248, 104, 60, "SETUP",   COL_MUTED };

const Button BTN_ADM_TRIG_DN  = { 330,  46, 64, 60, "-", COL_ACCENT };
const Button BTN_ADM_TRIG_UP  = { 402,  46, 64, 60, "+", COL_ACCENT };
const Button BTN_ADM_DWELL_DN = { 330, 118, 64, 60, "-", COL_ACCENT };
const Button BTN_ADM_DWELL_UP = { 402, 118, 64, 60, "+", COL_ACCENT };
const Button BTN_ADM_SCAN_DN  = { 330, 190, 64, 60, "-", COL_ACCENT };
const Button BTN_ADM_SCAN_UP  = { 402, 190, 64, 60, "+", COL_ACCENT };
const Button BTN_ADM_TOUCH    = { 166, 258, 158, 56, "TOUCH SETUP", COL_MUTED };
const Button BTN_ADM_STATS    = { 332, 258, 134, 56, "STATS",       COL_MUTED };
const Button BTN_ADM_BACK     = {  14, 258, 140, 56, "BACK",        COL_MUTED };

const Button BTN_STATS_RESET  = { 300, 258, 166, 56, "CLEAR COUNT", COL_WARN  };
const Button BTN_STATS_BACK   = {  14, 258, 140, 56, "BACK",        COL_MUTED };

// Progress bar geometry, shared by the gate screens.
static const int16_t GBAR_X = 20, GBAR_W = 440, GBAR_H = 26;

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

// ---------------------------------------------------------------------------
// Everyday use at the gate
//
// Nobody is operating these. The distance sensor decides what happens; the
// screen only says what is going on, in words readable from arm's length by
// someone who has never been shown how it works.
// ---------------------------------------------------------------------------

// Fills a progress bar without repainting the whole thing, so nothing flickers:
// the filled part and the empty part are drawn as two rectangles that never
// overlap. Call drawGateBarFrame() once first for the outline.
static void drawGateBarFrame(Display& tft, int16_t y) {
    tft.drawRoundRect(GBAR_X, y, GBAR_W, GBAR_H, 6, COL_MUTED);
}

static void drawGateBarFill(Display& tft, int16_t y, float fraction, uint16_t colour) {
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;

    const int16_t inner = GBAR_W - 6;
    const int16_t done  = (int16_t)(inner * fraction);

    if (done > 0) {
        tft.fillRect(GBAR_X + 3, y + 3, done, GBAR_H - 6, colour);
    }
    if (done < inner) {
        tft.fillRect(GBAR_X + 3 + done, y + 3, inner - done, GBAR_H - 6, COL_BG);
    }
}

void drawReady(Display& tft, const UiState& s) {
    tft.fillScreen(COL_BG);
    drawStatusBar(tft, s, "READY");

    text(tft, "PLACE BAG IN TRAY", SCREEN_W / 2, 108, 4, 1, COL_TEXT, COL_BG, MC_DATUM);

    drawReadyDistance(tft, s);

    // Small, in the corners, and nothing in normal use needs them. Collection
    // is for when data has been lost or the board has been reset; setup is for
    // the numbers that decide when a scan starts.
    drawButton(tft, BTN_READY_COLLECT, false);
    drawButton(tft, BTN_READY_SETUP,   false);

    text(tft, BUILD_TAG, SCREEN_W / 2, 308, 2, 1, COL_MUTED, COL_BG, MC_DATUM);
}

// Refreshed several times a second so the number follows a bag being moved
// about. Repaints its own strip only.
void drawReadyDistance(Display& tft, const UiState& s) {
    char buf[40];

    tft.fillRect(60, 142, 360, 68, COL_BG);

    if (!s.sensorWorking) {
        text(tft, "distance sensor not found", SCREEN_W / 2, 158, 2, 1,
             COL_WARN, COL_BG, MC_DATUM);
        text(tft, "scanning cannot start", SCREEN_W / 2, 186, 2, 1,
             COL_MUTED, COL_BG, MC_DATUM);
        return;
    }

    if (s.sensorInRange) {
        text(tft, "tray", SCREEN_W / 2, 156, 2, 1, COL_MUTED, COL_BG, MC_DATUM);
        snprintf(buf, sizeof(buf), "%u mm", s.distanceMm);
        text(tft, buf, SCREEN_W / 2, 188, 4, 1, COL_ACCENT, COL_BG, MC_DATUM);
    } else {
        text(tft, "tray is clear", SCREEN_W / 2, 172, 2, 1, COL_MUTED, COL_BG, MC_DATUM);
    }
}

void drawArmingFrame(Display& tft, const UiState& s) {
    tft.fillScreen(COL_BG);
    drawStatusBar(tft, s, "BAG DETECTED");

    text(tft, "HOLD STILL", SCREEN_W / 2, 104, 4, 1, COL_WARN, COL_BG, MC_DATUM);
    text(tft, "starting scan", SCREEN_W / 2, 140, 2, 1, COL_MUTED, COL_BG, MC_DATUM);

    drawGateBarFrame(tft, 168);
    text(tft, BUILD_TAG, SCREEN_W / 2, 308, 2, 1, COL_MUTED, COL_BG, MC_DATUM);
}

void drawArmingLive(Display& tft, const UiState& s) {
    char buf[24];
    float fraction = (s.dwellMs == 0) ? 1.0f : (float)s.phaseMs / (float)s.dwellMs;
    drawGateBarFill(tft, 168, fraction, COL_WARN);

    tft.fillRect(140, 214, 200, 30, COL_BG);
    snprintf(buf, sizeof(buf), "%u mm", s.distanceMm);
    text(tft, buf, SCREEN_W / 2, 228, 4, 1, COL_ACCENT, COL_BG, MC_DATUM);
}

void drawGateScanFrame(Display& tft, const UiState& s) {
    tft.fillScreen(COL_BG);
    drawStatusBar(tft, s, "SCANNING");

    text(tft, "SCANNING", SCREEN_W / 2, 104, 4, 1, COL_TEXT, COL_BG, MC_DATUM);
    text(tft, "do not move the bag", SCREEN_W / 2, 140, 2, 1, COL_MUTED, COL_BG, MC_DATUM);

    drawGateBarFrame(tft, 168);

    // No cancel button here either, for the same reason as the collection
    // screen: the firmware has no cancel path, so every scan ends the same way.
    text(tft, "finishes on its own", SCREEN_W / 2, 286, 2, 1, COL_MUTED, COL_BG, MC_DATUM);
    text(tft, BUILD_TAG, SCREEN_W / 2, 308, 2, 1, COL_MUTED, COL_BG, MC_DATUM);
}

void drawGateScanLive(Display& tft, const UiState& s) {
    char buf[24];
    float fraction = (s.scanMs == 0) ? 1.0f : (float)s.phaseMs / (float)s.scanMs;
    drawGateBarFill(tft, 168, fraction, COL_ACCENT);

    uint32_t leftMs = (s.phaseMs >= s.scanMs) ? 0 : (s.scanMs - s.phaseMs);
    tft.fillRect(140, 214, 200, 26, COL_BG);
    snprintf(buf, sizeof(buf), "%u.%u s left",
             (unsigned)(leftMs / 1000), (unsigned)((leftMs % 1000) / 100));
    text(tft, buf, SCREEN_W / 2, 226, 2, 1, COL_MUTED, COL_BG, MC_DATUM);
}

// The result.
//
// This is the one screen that has to be read across a room by someone who is
// not looking for it, so it is a block of colour with one word on it rather
// than a layout. The colour is the message; the word confirms it.
//
// The footer stays dark through all three levels. It carries the two lines
// that must stay readable while the red is flashing behind everything else.
static const int16_t SIGNAL_H = 252;

static void drawResultSignal(Display& tft, const UiState& s, bool bright) {
    uint16_t ground, ink;

    switch (s.verdict) {
        case VERDICT_CLEAR:
            ground = COL_OK;   ink = COL_BG;
            break;
        case VERDICT_UNSURE:
            ground = COL_WARN; ink = COL_BG;
            break;
        default:
            // Flashing: bright red with dark lettering, then dark red with
            // light lettering. Both halves stay legible, so the word can be
            // read at any moment rather than only half the time.
            ground = bright ? COL_BAD   : COL_BAD_DIM;
            ink    = bright ? COL_BG    : COL_TEXT;
            break;
    }

    tft.fillRect(0, 0, SCREEN_W, SIGNAL_H, ground);
    text(tft, verdictWord(s.verdict),   SCREEN_W / 2, 104, 4, 2, ink, ground, MC_DATUM);
    text(tft, verdictAdvice(s.verdict), SCREEN_W / 2, 180, 2, 1, ink, ground, MC_DATUM);
}

void drawGateResult(Display& tft, const UiState& s) {
    drawResultSignal(tft, s, true);

    tft.fillRect(0, SIGNAL_H, SCREEN_W, SCREEN_H - SIGNAL_H, COL_BG);
    drawGateResultPrompt(tft, s);

    // The whole point of this line. Everything above it is invented, and
    // anyone reading the screen is told so in the same glance. It comes off
    // when a detector has been trained and tested, and not one moment before.
    text(tft, "STAND-IN RESULT - NO DETECTOR TRAINED",
         SCREEN_W / 2, 302, 2, 1, COL_WARN, COL_BG, MC_DATUM);
}

// Called a few times a second while a red result is showing. Repaints only the
// coloured area, so the footer underneath never flickers and stays readable.
void drawGateResultFlash(Display& tft, const UiState& s, bool bright) {
    drawResultSignal(tft, s, bright);
}

// The bottom line changes once the machine starts waiting for the bag to be
// taken away, so the rest of the screen does not have to be repainted.
void drawGateResultPrompt(Display& tft, const UiState& s) {
    tft.fillRect(40, SIGNAL_H + 8, 400, 26, COL_BG);

    if (s.distanceMm < s.rearmMm && s.sensorInRange) {
        text(tft, "REMOVE BAG TO CONTINUE", SCREEN_W / 2, SIGNAL_H + 20, 2, 1,
             COL_ACCENT, COL_BG, MC_DATUM);
    } else {
        text(tft, "holding result", SCREEN_W / 2, SIGNAL_H + 20, 2, 1,
             COL_MUTED, COL_BG, MC_DATUM);
    }
}

// ---------------------------------------------------------------------------
// SETUP
//
// Three numbers, changed by pressing. They live here rather than on a web page
// because a web page would mean this board serving traffic on the same channel
// it is trying to measure - see the note in docs/START-HERE.md.
// ---------------------------------------------------------------------------
static void drawAdminRow(Display& tft, int16_t y, const char* label,
                         const char* value, const Button& down, const Button& up) {
    text(tft, label, 20, y + 18, 2, 1, COL_MUTED, COL_BG, ML_DATUM);
    text(tft, value, 20, y + 44, 4, 1, COL_TEXT, COL_BG, ML_DATUM);
    drawButton(tft, down, false);
    drawButton(tft, up,   false);
}

void drawAdmin(Display& tft, const UiState& s) {
    char buf[24];

    tft.fillScreen(COL_BG);
    drawStatusBar(tft, s, "SETUP");

    snprintf(buf, sizeof(buf), "%u mm", s.triggerMm);
    drawAdminRow(tft, 46, "SCAN STARTS CLOSER THAN", buf,
                 BTN_ADM_TRIG_DN, BTN_ADM_TRIG_UP);

    snprintf(buf, sizeof(buf), "%u ms", s.dwellMs);
    drawAdminRow(tft, 118, "BAG MUST HOLD STILL FOR", buf,
                 BTN_ADM_DWELL_DN, BTN_ADM_DWELL_UP);

    snprintf(buf, sizeof(buf), "%u.%u s", s.scanMs / 1000, (s.scanMs % 1000) / 100);
    drawAdminRow(tft, 190, "A SCAN LASTS", buf,
                 BTN_ADM_SCAN_DN, BTN_ADM_SCAN_UP);

    drawButton(tft, BTN_ADM_BACK,  false);
    drawButton(tft, BTN_ADM_TOUCH, false);
    drawButton(tft, BTN_ADM_STATS, false);
}

// ---------------------------------------------------------------------------
// The tally
//
// How many bags this machine has looked at, the way a printer knows its page
// count. Percentages are given to whole numbers: a figure like 12.7% would
// imply a precision these counts do not have.
// ---------------------------------------------------------------------------
static uint32_t percentOf(uint32_t part, uint32_t whole) {
    return whole ? (uint32_t)((part * 100UL + whole / 2) / whole) : 0;
}

static void drawTallyRow(Display& tft, int16_t y, const char* label, uint16_t colour,
                         uint32_t count, uint32_t total) {
    char buf[24];

    tft.fillRect(24, y, 10, 22, colour);
    text(tft, label, 46, y + 11, 2, 1, COL_TEXT, COL_BG, ML_DATUM);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)count);
    text(tft, buf, 330, y + 11, 4, 1, COL_TEXT, COL_BG, MR_DATUM);

    snprintf(buf, sizeof(buf), "%lu%%", (unsigned long)percentOf(count, total));
    text(tft, buf, 456, y + 11, 2, 1, COL_MUTED, COL_BG, MR_DATUM);
}

void drawStats(Display& tft, const UiState& s, const ScanStats& stats) {
    char buf[48];

    tft.fillScreen(COL_BG);
    drawStatusBar(tft, s, "TALLY");

    snprintf(buf, sizeof(buf), "%lu bags scanned", (unsigned long)stats.total);
    text(tft, buf, 24, 62, 4, 1, COL_TEXT, COL_BG, ML_DATUM);

    if (!stats.loaded) {
        text(tft, "this session only - not saved to the card",
             24, 88, 2, 1, COL_WARN, COL_BG, ML_DATUM);
    } else {
        text(tft, "kept on the card, survives power off",
             24, 88, 2, 1, COL_MUTED, COL_BG, ML_DATUM);
    }

    drawTallyRow(tft, 112, "CLEAR",  COL_OK,   stats.byLevel[0], stats.total);
    drawTallyRow(tft, 148, "CHECK",  COL_WARN, stats.byLevel[1], stats.total);
    drawTallyRow(tft, 184, "SEARCH", COL_BAD,  stats.byLevel[2], stats.total);

    // The line that stops this screen from being mistaken for evidence. Every
    // one of those counts came from a detector that does not exist yet.
    if (stats.standIn) {
        snprintf(buf, sizeof(buf), "%lu of these were stand-in answers",
                 (unsigned long)stats.standIn);
        text(tft, buf, SCREEN_W / 2, 224, 2, 1, COL_WARN, COL_BG, MC_DATUM);
        text(tft, "no detector has been trained - they measure nothing",
             SCREEN_W / 2, 242, 2, 1, COL_MUTED, COL_BG, MC_DATUM);
    }

    drawButton(tft, BTN_STATS_BACK,  false);
    drawButton(tft, BTN_STATS_RESET, false);
}

// A single line on the resting screen, so the count is visible without going
// looking for it. Its own strip, repainted on its own.
void drawReadyTally(Display& tft, const ScanStats& stats) {
    char buf[48];
    tft.fillRect(60, 214, 360, 22, COL_BG);

    if (stats.total == 0) return;

    snprintf(buf, sizeof(buf), "%lu scanned   %lu flagged   %lu%%",
             (unsigned long)stats.total,
             (unsigned long)stats.byLevel[2],
             (unsigned long)percentOf(stats.byLevel[2], stats.total));
    text(tft, buf, SCREEN_W / 2, 225, 2, 1, COL_MUTED, COL_BG, MC_DATUM);
}
