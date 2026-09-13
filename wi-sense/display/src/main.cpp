// ---------------------------------------------------------------------------
// Wi-Sense display prototype
//
// Runs on one ESP32 with the 4.0" screen attached. It shows the real screens
// of the finished machine, driven by stand-in numbers, so the layout and the
// touch behaviour can be judged before any radio work is done.
//
// It does no WiFi, no CSI, and no detection. Those arrive later, and when they
// do, only ui_state.cpp changes — the screens stay as they are.
//
// Pins come from docs/wiring-pin-map.md. The screen pins are set in
// platformio.ini because the graphics library reads them at build time.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <string.h>

#include "theme.h"
#include "board_pins.h"
#include "ui_state.h"
#include "screens.h"

static TFT_eSPI tft = TFT_eSPI();
static Preferences prefs;

static UiState  state;
static ScreenId screen = SCR_BOOT;
static bool     needsRepaint = true;

static uint32_t screenEnteredAt = 0;
static uint32_t lastTickAt      = 0;
static uint32_t lastTouchAt     = 0;

static void goTo(ScreenId next) {
    screen = next;
    needsRepaint = true;
    screenEnteredAt = millis();
}

static void showMessage(const char* title, const char* body) {
    strncpy(state.messageTitle, title, sizeof(state.messageTitle) - 1);
    state.messageTitle[sizeof(state.messageTitle) - 1] = '\0';
    strncpy(state.messageBody, body, sizeof(state.messageBody) - 1);
    state.messageBody[sizeof(state.messageBody) - 1] = '\0';
    goTo(SCR_MESSAGE);
}

// ---------------------------------------------------------------------------
// Indicator lights
//
// Blue means "ready to scan" and is safe to use now.
//
// Red and green are reserved for a bag verdict, and MUST STAY DARK until a
// detector has actually been trained and tested. A red light driven by a guess
// would be the one thing that discredits the whole project.
// ---------------------------------------------------------------------------
static void setLights(bool ready) {
    digitalWrite(PIN_LED_BLUE, ready ? HIGH : LOW);
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_RED,   LOW);

    // Once state.modelTrained is genuinely true, and only then, the verdict
    // would be shown here instead:
    //   digitalWrite(PIN_LED_GREEN, verdictIsClear ? HIGH : LOW);
    //   digitalWrite(PIN_LED_RED,   verdictIsClear ? LOW  : HIGH);
}

// ---------------------------------------------------------------------------
// Touch calibration. Resistive panels need this once; the result is kept in
// the chip's own small settings store so it survives a power cycle.
// Hold a finger on the screen while powering on to redo it.
// ---------------------------------------------------------------------------
static void setUpTouch(bool forceRecalibrate) {
    uint16_t calData[5];
    bool haveCal = false;

    prefs.begin("wisense", false);
    if (!forceRecalibrate && prefs.getBytesLength("touchcal") == sizeof(calData)) {
        prefs.getBytes("touchcal", calData, sizeof(calData));
        haveCal = true;
    }

    if (!haveCal) {
        tft.fillScreen(COL_BG);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(COL_TEXT, COL_BG);
        tft.drawString("Touch each corner arrow", SCREEN_W / 2, SCREEN_H / 2 - 20, 4);
        tft.drawString("with the stylus", SCREEN_W / 2, SCREEN_H / 2 + 20, 4);
        delay(1800);
        tft.fillScreen(COL_BG);

        tft.calibrateTouch(calData, COL_ACCENT, COL_BG, 20);

        prefs.putBytes("touchcal", calData, sizeof(calData));
    }
    prefs.end();

    tft.setTouch(calData);
}

// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_LED_RED,   OUTPUT);
    pinMode(PIN_LED_BLUE,  OUTPUT);
    setLights(false);

    pinMode(PIN_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_BACKLIGHT, HIGH);
    // To run the backlight dimmer instead, replace the line above with:
    //   analogWrite(PIN_BACKLIGHT, 180);

    tft.init();
    tft.setRotation(1);          // landscape: 480 wide, 320 tall
    tft.fillScreen(COL_BG);

    // A press held during power-on forces recalibration.
    bool held = (tft.getTouchRawZ() > 500);
    setUpTouch(held);

    simInit(state);
    randomSeed(micros());
    goTo(SCR_BOOT);

    Serial.println("display prototype ready");
}

// ---------------------------------------------------------------------------
void loop() {
    const uint32_t now = millis();
    const uint32_t sinceEnter = now - screenEnteredAt;

    // --- read the touch panel, with a simple debounce ---
    uint16_t tx = 0, ty = 0;
    bool tapped = false;
    if (tft.getTouch(&tx, &ty)) {
        if (now - lastTouchAt > 300) {
            lastTouchAt = now;
            tapped = true;
        }
    }

    switch (screen) {

    case SCR_BOOT: {
        if (simSelfTest(state, sinceEnter)) needsRepaint = true;
        if (needsRepaint) { drawBoot(tft, state); needsRepaint = false; }
        if (sinceEnter > 2600) goTo(SCR_HOME);
        break;
    }

    case SCR_HOME: {
        setLights(true);
        if (needsRepaint) { drawHome(tft, state); needsRepaint = false; }
        if (tapped) {
            if (buttonHit(BTN_HOME_COLLECT, tx, ty)) {
                goTo(SCR_LABEL);
            } else if (buttonHit(BTN_HOME_IDENTIFY, tx, ty)) {
                if (state.modelTrained) {
                    showMessage("NOT WIRED YET", "Detection arrives after the model is trained.");
                } else {
                    showMessage("MODEL NOT TRAINED", "Collect labelled runs first, then train.");
                }
            }
        }
        break;
    }

    case SCR_LABEL: {
        if (needsRepaint) { drawLabel(tft, state); needsRepaint = false; }
        if (tapped) {
            if (buttonHit(BTN_LABEL_EMPTY, tx, ty)) {
                state.category = CAT_EMPTY;
                strncpy(state.objectName, "none", sizeof(state.objectName) - 1);
                goTo(SCR_SETTINGS);
            } else if (buttonHit(BTN_LABEL_METAL, tx, ty)) {
                state.category = CAT_METAL;
                strncpy(state.objectName, "object", sizeof(state.objectName) - 1);
                goTo(SCR_SETTINGS);
            } else if (buttonHit(BTN_LABEL_NONMETAL, tx, ty)) {
                state.category = CAT_NON_METAL;
                strncpy(state.objectName, "object", sizeof(state.objectName) - 1);
                goTo(SCR_SETTINGS);
            } else if (buttonHit(BTN_LABEL_BACK, tx, ty)) {
                goTo(SCR_HOME);
            }
        }
        break;
    }

    case SCR_SETTINGS: {
        if (needsRepaint) { drawSettings(tft, state); needsRepaint = false; }
        if (tapped) {
            if      (buttonHit(BTN_SET_5S,   tx, ty)) { state.durationS = 5;  needsRepaint = true; }
            else if (buttonHit(BTN_SET_10S,  tx, ty)) { state.durationS = 10; needsRepaint = true; }
            else if (buttonHit(BTN_SET_20S,  tx, ty)) { state.durationS = 20; needsRepaint = true; }
            else if (buttonHit(BTN_SET_20HZ, tx, ty)) { state.rateHz = 20;    needsRepaint = true; }
            else if (buttonHit(BTN_SET_50HZ, tx, ty)) { state.rateHz = 50;    needsRepaint = true; }
            else if (buttonHit(BTN_SET_BACK, tx, ty)) { goTo(SCR_LABEL); }
            else if (buttonHit(BTN_SET_START, tx, ty)) {
                simStartRun(state);
                setLights(false);
                goTo(SCR_SCANNING);
                lastTickAt = now;
            }
        }
        break;
    }

    case SCR_SCANNING: {
        if (needsRepaint) {
            drawScanningFrame(tft, state);
            drawScanningLive(tft, state);
            needsRepaint = false;
        }

        // Update ten times a second, and only the small number boxes.
        if (now - lastTickAt >= 100) {
            simRunTick(state, now - lastTickAt);
            lastTickAt = now;
            drawScanningLive(tft, state);
        }

        if (state.elapsedMs >= (uint32_t)state.durationS * 1000UL) {
            simFinishRun(state);
            goTo(SCR_VERDICT);
        }
        break;
    }

    case SCR_VERDICT: {
        if (needsRepaint) { drawVerdict(tft, state); needsRepaint = false; }
        if (tapped) {
            if (buttonHit(BTN_V_AGAIN, tx, ty)) {
                simStartRun(state);
                goTo(SCR_SCANNING);
                lastTickAt = now;
            } else if (buttonHit(BTN_V_KEEP, tx, ty) ||
                       buttonHit(BTN_V_DISCARD, tx, ty)) {
                goTo(SCR_HOME);
            }
        }
        break;
    }

    case SCR_MESSAGE: {
        if (needsRepaint) { drawMessage(tft, state); needsRepaint = false; }
        if (tapped && buttonHit(BTN_MSG_OK, tx, ty)) goTo(SCR_HOME);
        break;
    }
    }

    delay(10);
}
