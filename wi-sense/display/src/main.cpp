// ---------------------------------------------------------------------------
// Wi-Sense display
//
// Shows the real screens of the finished machine, driven by stand-in numbers,
// so the layout can be judged before any radio work is done.
//
// This version draws with LovyanGFX. The library we used before could not
// drive the screen on the S3 — it crashed on start-up, and once that was
// worked around it still never produced a picture.
//
// Stage: screens + touch. The memory card and the distance sensor are still
// added back one at a time from here, so that anything that breaks has an
// obvious cause.
//
// Pins for both boards are in board_pins.h.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <string.h>
#include <Preferences.h>

#include "display_driver.h"
#include "theme.h"
#include "board_pins.h"
#include "ui_state.h"
#include "screens.h"
#include "storage.h"
#include "sensor.h"
#include "csi_capture.h"
#include "csi_sink.h"

// ---------------------------------------------------------------------------
// STAGE A - proving CSI capture works on this chip.
//
// Off by default, so this program still builds and behaves exactly as it did
// before the capture code moved in. Switch it on in platformio.ini and the
// board also becomes the old receiver: it joins the transmitter, answers the
// same serial commands collect_burst.py has always sent, and prints the same
// CSV rows. The screens are untouched and still run on invented numbers.
//
// It exists to answer one question and then go away - whether the readings
// come out of the S3 in the same shape they came out of the classic ESP32.
// Nothing downstream can be trusted until that is known. Stage D replaces all
// of this with the screen driving real runs.
// ---------------------------------------------------------------------------
#ifndef CSI_STAGE_A
#define CSI_STAGE_A 0
#endif

static const char CHAR_CR  = 13;
static const char CHAR_LF  = 10;
static const char CHAR_NUL = 0;

static Display    tft;
static UiState    state;
static ScreenId   screen = SCR_BOOT;
static bool       needsRepaint = true;
static CardInfo   cardInfo;
static ScanStats  scanStats;
static SensorInfo sensorInfo;

// ---------------------------------------------------------------------------
// Touch calibration. The XPT2046 chip returns raw pressure-sensor numbers,
// not screen pixels — calibrateTouch() maps one to the other by asking for a
// few taps, and the eight numbers it produces are what make that mapping
// stick. They are kept in the ESP32's own flash storage (a small reserved
// area meant for exactly this — settings that must survive a power cycle),
// under the name "touchcal", so a calibration done once does not need
// repeating after every reset.
// ---------------------------------------------------------------------------
static Preferences touchPrefs;
static uint16_t    touchCal[8];
static bool        touchCalibrated = false;

static void loadTouchCalibration() {
    touchPrefs.begin("touchcal", true);
    touchCalibrated = touchPrefs.isKey("cal") &&
        touchPrefs.getBytesLength("cal") == sizeof(touchCal) &&
        touchPrefs.getBytes("cal", touchCal, sizeof(touchCal)) == sizeof(touchCal);
    touchPrefs.end();

    if (touchCalibrated) {
        tft.setTouchCalibrate(touchCal);
        Serial.println("[touch] loaded saved calibration");
    } else {
        Serial.println("[touch] not calibrated yet - use TOUCH SETUP on the home screen");
    }
}

static void runTouchCalibration() {
    // This call itself draws the moving target markers and waits for a tap
    // on each one — nothing in screens.cpp is involved while it runs.
    tft.calibrateTouch(touchCal, TFT_WHITE, COL_BG, 20);

    touchPrefs.begin("touchcal", false);
    touchPrefs.putBytes("cal", touchCal, sizeof(touchCal));
    touchPrefs.end();

    touchCalibrated = true;
    Serial.println("[touch] calibration saved");
}

static uint32_t screenEnteredAt  = 0;
static uint32_t lastTickAt       = 0;
static uint32_t lastDiagAt       = 0;
static uint32_t lastSensorReadAt = 0;
static uint32_t lastGateDrawAt   = 0;
static bool     flashBright      = true;

// ---------------------------------------------------------------------------
// Gate settings.
//
// Kept in the board's own flash, the same way the touch calibration is, under
// the name "gate". The right values depend on where the sensor ends up sitting
// relative to the tray, so they are found by trying rather than by guessing -
// which is why they are changeable on the SETUP screen instead of compiled in.
// ---------------------------------------------------------------------------
static Preferences gatePrefs;

static void loadGateSettings() {
    // Opened read-write rather than read-only, purely so that the very first
    // boot of a board creates the store instead of printing an nvs_open
    // NOT_FOUND error that looks like a fault and is not one. Nothing is
    // written here; the defaults below are what a new board starts with.
    gatePrefs.begin("gate", false);
    state.triggerMm = gatePrefs.getUShort("trig",  GATE_TRIGGER_MM_DEFAULT);
    state.dwellMs   = gatePrefs.getUShort("dwell", GATE_DWELL_MS_DEFAULT);
    state.scanMs    = gatePrefs.getUShort("scan",  GATE_SCAN_MS_DEFAULT);
    gatePrefs.end();

    state.rearmMm = state.triggerMm + GATE_REARM_GAP_MM;

    Serial.print("[gate] trigger ");
    Serial.print(state.triggerMm);
    Serial.print("mm, re-arm ");
    Serial.print(state.rearmMm);
    Serial.print("mm, hold ");
    Serial.print(state.dwellMs);
    Serial.print("ms, scan ");
    Serial.print(state.scanMs);
    Serial.println("ms");
}

static void saveGateSettings() {
    gatePrefs.begin("gate", false);
    gatePrefs.putUShort("trig",  state.triggerMm);
    gatePrefs.putUShort("dwell", state.dwellMs);
    gatePrefs.putUShort("scan",  state.scanMs);
    gatePrefs.end();

    state.rearmMm = state.triggerMm + GATE_REARM_GAP_MM;
    Serial.println("[gate] settings saved");
    storageLog("gate settings changed");
}

static uint16_t stepValue(uint16_t value, int32_t by, uint16_t lo, uint16_t hi) {
    int32_t next = (int32_t)value + by;
    if (next < (int32_t)lo) next = lo;
    if (next > (int32_t)hi) next = hi;
    return (uint16_t)next;
}

// ---------------------------------------------------------------------------
// The distance sensor, as the gate screens use it.
// ---------------------------------------------------------------------------

// Takes a reading no more often than intervalMs. Returns true when a new one
// was actually taken, so a caller knows whether anything needs redrawing.
// sensorRead() blocks for about 4ms, which is why this is paced rather than
// called every time round the loop.
static bool pollSensor(uint32_t now, uint32_t intervalMs) {
    if (!state.sensorWorking) return false;
    if (now - lastSensorReadAt < intervalMs) return false;

    lastSensorReadAt = now;
    sensorRead(sensorInfo);
    state.sensorInRange = sensorInfo.inRange;
    state.distanceMm    = sensorInfo.distanceMm;
    return true;
}

// Something is close enough to count as a bag.
static bool bagPresent() {
    return state.sensorWorking && state.sensorInRange &&
           state.distanceMm < state.triggerMm;
}

// The tray is clear enough to arm again. Note this uses rearmMm, which sits
// further out than triggerMm on purpose: with one threshold for both, a bag
// left near the edge makes the reading cross back and forth and the same bag
// gets scanned over and over.
static bool trayClear() {
    if (!state.sensorWorking) return false;
    return !state.sensorInRange || state.distanceMm >= state.rearmMm;
}

// How long each screen holds before moving on, while there is no touch panel.
static const uint32_t AUTO_ADVANCE_MS = 4000;

static void logAction(const char* what) {
    Serial.print("> ");
    Serial.println(what);
}

// Printed at start-up and then every ten seconds, because the start-up text
// usually scrolls past before a monitor can attach to this board.
#if CSI_STAGE_A
// True between CSI_CAPTURE_ON and CSI_CAPTURE_OFF. While it is set, nothing
// else may print: a diagnostics line landing in the middle of a reading would
// corrupt that row for whatever is parsing the other end.
static bool captureRunning = false;

// The same three commands the receiver firmware has always answered, so the
// laptop tools drive this board without knowing anything changed.
static char commandBuffer[32];
static size_t commandLength = 0;

static void processCaptureCommands() {
    while (Serial.available() > 0) {
        char character = (char)Serial.read();
        if (character == CHAR_CR) {
            continue;
        }
        if (character == CHAR_LF) {
            commandBuffer[commandLength] = CHAR_NUL;
            if (strcmp(commandBuffer, "CSI_CAPTURE_ON") == 0) {
                captureRunning = true;
                csiCaptureStart();
                Serial.println("CSI_CAPTURE_READY");
            } else if (strcmp(commandBuffer, "CSI_CAPTURE_OFF") == 0) {
                bool stopped = csiCaptureStop();
                captureRunning = false;
                Serial.print(stopped ? "CSI_CAPTURE_STOPPED,drops=" : "CSI_CAPTURE_STOP_TIMEOUT,drops=");
                Serial.println(csiCaptureDrops());
            } else if (strcmp(commandBuffer, "CSI_STATUS") == 0) {
                Serial.print("RECEIVER_STATUS,ready=");
                Serial.print(csiCaptureIsReady() ? 1 : 0);
                Serial.print(",wifi=");
                Serial.println(csiCaptureIsConnected() ? 1 : 0);
            }
            commandLength = 0;
        } else if (commandLength < sizeof(commandBuffer) - 1) {
            commandBuffer[commandLength++] = character;
        }
    }
}
#endif

static void printDiagnostics() {
    Serial.print("DIAG  ");
    Serial.print(BUILD_TAG);
    Serial.print("   screen pins SCLK=");
    Serial.print(PIN_TFT_SCLK);
    Serial.print(" MOSI=");
    Serial.print(PIN_TFT_MOSI);
    Serial.print(" MISO=");
    Serial.print(PIN_TFT_MISO);
    Serial.print(" CS=");
    Serial.print(PIN_TFT_CS);
    Serial.print(" DC=");
    Serial.print(PIN_TFT_DC);
    Serial.print(" RST=");
    Serial.print(PIN_TFT_RST);
    Serial.print(" T_CS=");
    Serial.print(PIN_TOUCH_CS);
    Serial.print("   size ");
    Serial.print(tft.width());
    Serial.print("x");
    Serial.print(tft.height());
    Serial.print("   touch ");
    Serial.print(touchCalibrated ? "calibrated" : "NOT calibrated");
    Serial.print("   card ");
    Serial.print(state.cardWorking ? "mounted" : "NOT mounted");
    Serial.print("   sensor ");
    Serial.print(state.sensorWorking ? "ready" : "NOT found");
    // Repeated here rather than only at start-up, because the start-up lines
    // scroll past before a monitor can usually attach to this board.
    Serial.print("   tally ");
    Serial.print(scanStats.total);
    Serial.println(scanStats.loaded ? " (on card)" : " (this session only)");
}

static void goTo(ScreenId next) {
    screen = next;
    needsRepaint = true;
    screenEnteredAt = millis();
}

// ---------------------------------------------------------------------------
// Indicator lights
//
// Blue means "ready to scan" and is safe to use now.
//
// Red and green are reserved for a bag verdict and MUST STAY DARK until a
// detector has genuinely been trained and tested. A red light driven by a
// guess would be the one thing that discredits the whole project.
// ---------------------------------------------------------------------------
static void lampTest(uint32_t elapsedMs) {
    digitalWrite(PIN_LED_RED,   (elapsedMs >  200 && elapsedMs <  900) ? HIGH : LOW);
    digitalWrite(PIN_LED_GREEN, (elapsedMs >  900 && elapsedMs < 1600) ? HIGH : LOW);
    digitalWrite(PIN_LED_BLUE,  (elapsedMs > 1600 && elapsedMs < 2300) ? HIGH : LOW);
}

static void setLights(bool ready) {
    digitalWrite(PIN_LED_BLUE,  ready ? HIGH : LOW);
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_RED,   LOW);
}

// ---------------------------------------------------------------------------
// The screen's own test. Colour fills, then a marker in each corner. If a
// corner is missing or the picture is shifted, the size or rotation is wrong
// rather than the wiring.
// ---------------------------------------------------------------------------
static void runDisplayTest() {
    const uint16_t fills[4] = { TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE };
    for (int i = 0; i < 4; i++) {
        tft.fillScreen(fills[i]);
        delay(350);
    }

    tft.fillScreen(COL_BG);
    tft.fillRect(0, 0, 40, 40, COL_ACCENT);
    tft.fillRect(SCREEN_W - 40, 0, 40, 40, COL_ACCENT);
    tft.fillRect(0, SCREEN_H - 40, 40, 40, COL_ACCENT);
    tft.fillRect(SCREEN_W - 40, SCREEN_H - 40, 40, 40, COL_ACCENT);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.drawString("SCREEN OK", SCREEN_W / 2, SCREEN_H / 2 - 20, 4);
    tft.drawString("480 x 320", SCREEN_W / 2, SCREEN_H / 2 + 20, 2);
    tft.setTextColor(COL_MUTED, COL_BG);
    tft.drawString(BUILD_TAG, SCREEN_W / 2, SCREEN_H - 30, 2);
    delay(1500);
}

// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println();
    Serial.print("[1] board awake   ");
    Serial.println(BUILD_TAG);

    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_LED_RED,   OUTPUT);
    pinMode(PIN_LED_BLUE,  OUTPUT);
    setLights(false);

    pinMode(PIN_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_BACKLIGHT, HIGH);

    Serial.println("[2] starting the screen");
    bool ok = tft.init();
    tft.setRotation(1);              // landscape: 480 wide, 320 tall
    Serial.print("    init returned ");
    Serial.println(ok ? "true" : "false");
    loadTouchCalibration();
    printDiagnostics();

    runDisplayTest();
    Serial.println("[3] screen test finished");

    simInit(state);
    randomSeed(micros());

    // The card is real, unlike most of what's on this screen so far.
    Serial.println("[card] mounting...");
    bool cardOk = storageBegin(cardInfo) && storageSelfTest(cardInfo);
    state.cardPresent = cardOk;
    state.cardWorking  = cardOk;
    if (cardOk) {
        snprintf(state.cardDetail, sizeof(state.cardDetail), "%s card, %lu MB free",
                 cardInfo.type, (unsigned long)cardInfo.freeMB);
        Serial.println("[card] mounted, self-test passed");
        storageLog("boot - card mounted, self-test passed");
    } else {
        snprintf(state.cardDetail, sizeof(state.cardDetail), "%s",
                 cardInfo.error[0] ? cardInfo.error : "not mounted");
        Serial.print("[card] ");
        Serial.println(cardInfo.error);
    }

    if (storageStatsLoad(scanStats)) {
        Serial.print("[tally] ");
        Serial.print(scanStats.total);
        Serial.print(" scans on this card (");
        Serial.print(scanStats.standIn);
        Serial.println(" of them stand-in)");
    } else {
        Serial.println("[tally] no card - counts will be this session only");
    }

    Serial.println("[sensor] starting...");
    bool sensorOk = sensorBegin(sensorInfo);
    state.sensorWorking = sensorOk;
    if (sensorOk) {
        snprintf(state.sensorDetail, sizeof(state.sensorDetail), "distance sensor ready");
        Serial.println("[sensor] answered, ready");
        storageLog("boot - distance sensor ready");
    } else {
        snprintf(state.sensorDetail, sizeof(state.sensorDetail), "%s",
                 sensorInfo.error[0] ? sensorInfo.error : "not found");
        Serial.print("[sensor] ");
        Serial.println(sensorInfo.error);
    }

#if CSI_STAGE_A
    Serial.println("[csi] joining the transmitter - this can take ten seconds...");
    csiSinkSet(csiSinkSerial);
    csiCaptureInit();
    Serial.print("[csi] ");
    Serial.print(csiCaptureIsConnected() ? "joined" : "NOT joined");
    Serial.print(", capture ");
    Serial.println(csiCaptureIsReady() ? "ready" : "NOT ready");
    storageLog(csiCaptureIsReady() ? "boot - csi ready" : "boot - csi NOT ready");
#endif

    loadGateSettings();

    goTo(SCR_BOOT);
    Serial.println(touchCalibrated
        ? "[4] ready - touch is calibrated, screens wait for a press"
        : "[4] ready - not calibrated yet, screens advance on a timer");
}

// ---------------------------------------------------------------------------
// A touch only counts the moment a finger comes down, not for every poll
// while it is held — otherwise one press would fire a button many times.
// ---------------------------------------------------------------------------
static bool touchWasDown = false;

void loop() {
    const uint32_t now = millis();
    const uint32_t sinceEnter = now - screenEnteredAt;

#if CSI_STAGE_A
    processCaptureCommands();
#endif

    if (now - lastDiagAt > 10000) {
        lastDiagAt = now;
#if CSI_STAGE_A
        if (!captureRunning)
#endif
        printDiagnostics();
    }

    int32_t tx32 = 0, ty32 = 0;
    bool touchedNow   = tft.getTouch(&tx32, &ty32);
    bool touchPressed = touchedNow && !touchWasDown;   // rising edge only
    touchWasDown = touchedNow;
    int16_t tx = (int16_t)tx32;
    int16_t ty = (int16_t)ty32;

#if TOUCH_DEBUG
    if (touchPressed) {
        Serial.print("[touch] x="); Serial.print(tx);
        Serial.print(" y=");        Serial.println(ty);
    }
#endif

    switch (screen) {

    case SCR_BOOT: {
        lampTest(sinceEnter);
        if (simSelfTest(state, sinceEnter)) needsRepaint = true;
        if (needsRepaint) { drawBoot(tft, state); needsRepaint = false; }
        if (sinceEnter > 3000) {
            // A board that has never been calibrated lands on SETUP instead,
            // because TOUCH SETUP lives there now and a screen that cannot be
            // pressed accurately has no other way of reaching it.
            if (touchCalibrated) { logAction("ready"); goTo(SCR_READY); }
            else                 { logAction("setup - not calibrated"); goTo(SCR_ADMIN); }
        }
        break;
    }

    // -----------------------------------------------------------------------
    // Everyday use at the gate.
    //
    // Nobody operates these. The distance sensor decides what happens and the
    // screen only says what is going on. The collection screens below are
    // still all there, behind the small COLLECT button.
    // -----------------------------------------------------------------------
    case SCR_READY: {
        setLights(true);
        if (needsRepaint) {
            drawReady(tft, state);
            drawReadyTally(tft, scanStats);
            needsRepaint = false;
        }

        if (pollSensor(now, 100)) {
            drawReadyDistance(tft, state);
            if (bagPresent()) {
                logAction("bag detected");
                goTo(SCR_ARMING);
                break;
            }
        }

        if (touchPressed && buttonHit(BTN_READY_COLLECT, tx, ty)) {
            logAction("COLLECT (touch)");
            goTo(SCR_LABEL);
        } else if (touchPressed && buttonHit(BTN_READY_SETUP, tx, ty)) {
            logAction("SETUP (touch)");
            goTo(SCR_ADMIN);
        }
        break;
    }

    case SCR_ARMING: {
        setLights(false);
        if (needsRepaint) {
            drawArmingFrame(tft, state);
            state.phaseMs = 0;
            drawArmingLive(tft, state);
            lastGateDrawAt = now;
            needsRepaint = false;
        }

        pollSensor(now, 60);

        // Gone again before the hold time was up. This is the whole point of
        // the hold: a hand reaching across the tray must not start a scan.
        if (!bagPresent()) {
            logAction("bag removed before scan started");
            goTo(SCR_READY);
            break;
        }

        state.phaseMs = sinceEnter;
        if (now - lastGateDrawAt >= 80) {
            lastGateDrawAt = now;
            drawArmingLive(tft, state);
        }

        if (sinceEnter >= state.dwellMs) {
            gateDecide(state);
            logAction("scan starting");
            goTo(SCR_GATE_SCAN);
        }
        break;
    }

    case SCR_GATE_SCAN: {
        if (needsRepaint) {
            drawGateScanFrame(tft, state);
            state.phaseMs = 0;
            drawGateScanLive(tft, state);
            lastGateDrawAt = now;
            needsRepaint = false;
        }

        state.phaseMs = sinceEnter;
        if (now - lastGateDrawAt >= 100) {
            lastGateDrawAt = now;
            drawGateScanLive(tft, state);
        }

        // No cancel path, matching the collection screen and the capture
        // firmware: a started scan always ends the same way.
        if (sinceEnter >= state.scanMs) {
            // "stand-in" is the detector's name until a real one exists, and
            // it is written into every row and the totals. A count of invented
            // answers must never be mistaken later for a record of real ones.
            storageStatsRecord(scanStats, (uint8_t)state.verdict, state.confidence,
                               state.distanceMm, "stand-in");

            char line[72];
            snprintf(line, sizeof(line), "(sim) gate scan %lu - stand-in result %s",
                     (unsigned long)scanStats.total, verdictWord(state.verdict));
            storageLog(line);
            goTo(SCR_GATE_RESULT);
        }
        break;
    }

    case SCR_GATE_RESULT: {
        if (needsRepaint) {
            drawGateResult(tft, state);
            flashBright   = true;
            lastGateDrawAt = now;
            needsRepaint  = false;
        }

        // Only the red result moves. Green and orange are deliberately steady:
        // a bag that is fine should not make the machine look agitated.
        if (state.verdict == VERDICT_METAL && now - lastGateDrawAt >= GATE_FLASH_MS) {
            lastGateDrawAt = now;
            flashBright = !flashBright;
            drawGateResultFlash(tft, state, flashBright);
        }

        if (pollSensor(now, 120)) {
            drawGateResultPrompt(tft, state);

            // Held for a moment even if the bag is snatched away, so a result
            // cannot flash past unseen - then held until the tray is properly
            // clear, so the same bag cannot be scanned twice.
            if (sinceEnter > 800 && trayClear()) {
                logAction("tray clear - ready again");
                goTo(SCR_READY);
            }
        }
        break;
    }

    case SCR_ADMIN: {
        setLights(false);
        if (needsRepaint) { drawAdmin(tft, state); needsRepaint = false; }

        bool changed = false;
        if (touchPressed) {
            if (buttonHit(BTN_ADM_TRIG_DN, tx, ty)) {
                state.triggerMm = stepValue(state.triggerMm, -(int32_t)GATE_TRIGGER_MM_STEP,
                                            GATE_TRIGGER_MM_MIN, GATE_TRIGGER_MM_MAX);
                changed = true;
            } else if (buttonHit(BTN_ADM_TRIG_UP, tx, ty)) {
                state.triggerMm = stepValue(state.triggerMm, GATE_TRIGGER_MM_STEP,
                                            GATE_TRIGGER_MM_MIN, GATE_TRIGGER_MM_MAX);
                changed = true;
            } else if (buttonHit(BTN_ADM_DWELL_DN, tx, ty)) {
                state.dwellMs = stepValue(state.dwellMs, -(int32_t)GATE_DWELL_MS_STEP,
                                          GATE_DWELL_MS_MIN, GATE_DWELL_MS_MAX);
                changed = true;
            } else if (buttonHit(BTN_ADM_DWELL_UP, tx, ty)) {
                state.dwellMs = stepValue(state.dwellMs, GATE_DWELL_MS_STEP,
                                          GATE_DWELL_MS_MIN, GATE_DWELL_MS_MAX);
                changed = true;
            } else if (buttonHit(BTN_ADM_SCAN_DN, tx, ty)) {
                state.scanMs = stepValue(state.scanMs, -(int32_t)GATE_SCAN_MS_STEP,
                                         GATE_SCAN_MS_MIN, GATE_SCAN_MS_MAX);
                changed = true;
            } else if (buttonHit(BTN_ADM_SCAN_UP, tx, ty)) {
                state.scanMs = stepValue(state.scanMs, GATE_SCAN_MS_STEP,
                                         GATE_SCAN_MS_MIN, GATE_SCAN_MS_MAX);
                changed = true;
            } else if (buttonHit(BTN_ADM_TOUCH, tx, ty)) {
                logAction("TOUCH SETUP (touch)");
                goTo(SCR_TOUCH_SETUP);
                break;
            } else if (buttonHit(BTN_ADM_STATS, tx, ty)) {
                logAction("STATS (touch)");
                saveGateSettings();
                goTo(SCR_STATS);
                break;
            } else if (buttonHit(BTN_ADM_BACK, tx, ty)) {
                logAction("BACK (touch)");
                saveGateSettings();
                goTo(SCR_READY);
                break;
            }
        }

        if (changed) {
            state.rearmMm = state.triggerMm + GATE_REARM_GAP_MM;
            needsRepaint = true;
        } else if (!touchCalibrated && sinceEnter > AUTO_ADVANCE_MS) {
            logAction("touch setup (timer - not calibrated)");
            goTo(SCR_TOUCH_SETUP);
        }
        break;
    }

    case SCR_LABEL: {
        if (needsRepaint) { drawLabel(tft, state); needsRepaint = false; }

        Category picked = CAT_EMPTY;
        const char* pickedName = nullptr;
        bool pick = false;

        if (touchPressed && buttonHit(BTN_LABEL_EMPTY, tx, ty)) {
            picked = CAT_EMPTY; pickedName = "none"; pick = true;
        } else if (touchPressed && buttonHit(BTN_LABEL_METAL, tx, ty)) {
            picked = CAT_METAL; pickedName = "mug"; pick = true;
        } else if (touchPressed && buttonHit(BTN_LABEL_NONMETAL, tx, ty)) {
            picked = CAT_NON_METAL; pickedName = "bottle"; pick = true;
        } else if (touchPressed && buttonHit(BTN_LABEL_BACK, tx, ty)) {
            logAction("BACK (touch)");
            goTo(SCR_READY);
        }

        if (pick) {
            state.category = picked;
            strncpy(state.objectName, pickedName, sizeof(state.objectName) - 1);
            logAction("label picked (touch)");
            goTo(SCR_SETTINGS);
        } else if (!touchCalibrated && sinceEnter > AUTO_ADVANCE_MS) {
            state.category = CAT_METAL;
            strncpy(state.objectName, "mug", sizeof(state.objectName) - 1);
            logAction("label = METAL (timer - not calibrated)");
            goTo(SCR_SETTINGS);
        }
        break;
    }

    case SCR_SETTINGS: {
        if (needsRepaint) { drawSettings(tft, state); needsRepaint = false; }

        bool started = false;

        if (touchPressed && buttonHit(BTN_SET_5S, tx, ty)) {
            state.durationS = 5;  needsRepaint = true;
        } else if (touchPressed && buttonHit(BTN_SET_10S, tx, ty)) {
            state.durationS = 10; needsRepaint = true;
        } else if (touchPressed && buttonHit(BTN_SET_20S, tx, ty)) {
            state.durationS = 20; needsRepaint = true;
        } else if (touchPressed && buttonHit(BTN_SET_20HZ, tx, ty)) {
            state.rateHz = 20;    needsRepaint = true;
        } else if (touchPressed && buttonHit(BTN_SET_50HZ, tx, ty)) {
            state.rateHz = 50;    needsRepaint = true;
        } else if (touchPressed && buttonHit(BTN_SET_BACK, tx, ty)) {
            logAction("BACK (touch)");
            goTo(SCR_LABEL);
        } else if (touchPressed && buttonHit(BTN_SET_START, tx, ty)) {
            logAction("START (touch)");
            started = true;
        } else if (!touchCalibrated && sinceEnter > AUTO_ADVANCE_MS) {
            logAction("START (timer - not calibrated)");
            started = true;
        }

        if (started) {
            simStartRun(state);
            setLights(false);
            goTo(SCR_SCANNING);
            lastTickAt = now;
        }
        break;
    }

    case SCR_SCANNING: {
        // No cancel button, on purpose - see the note in drawScanningFrame().
        if (needsRepaint) {
            drawScanningFrame(tft, state);
            drawScanningLive(tft, state);
            needsRepaint = false;
        }

        // Ten times a second, and only the small number boxes. A full repaint
        // mid-capture takes long enough to cost you readings.
        if (now - lastTickAt >= 100) {
            simRunTick(state, now - lastTickAt);
            lastTickAt = now;
            drawScanningLive(tft, state);
        }

        if (state.elapsedMs >= (uint32_t)state.durationS * 1000UL) {
            simFinishRun(state);
            Serial.print("> run finished: ");
            Serial.print(state.runPassed ? "PASS" : "FAIL");
            Serial.print("   rows ");
            Serial.print(state.rowsCaptured);
            Serial.print("/");
            Serial.print(state.packetsSent);
            Serial.print("   coverage ");
            Serial.print((int)(state.coverage * 100.0f));
            Serial.println("%");

            // Labelled "(sim)" on purpose - these are invented numbers, not a
            // real capture, and the log must not look otherwise later.
            char logLine[80];
            snprintf(logLine, sizeof(logLine), "(sim) run %s - %s/%s coverage %d%%",
                     state.runPassed ? "PASS" : "FAIL",
                     categoryFolder(state.category), state.objectName,
                     (int)(state.coverage * 100.0f));
            storageLog(logLine);

            goTo(SCR_VERDICT);
        }
        break;
    }

    case SCR_VERDICT: {
        if (needsRepaint) { drawVerdict(tft, state); needsRepaint = false; }

        // DISCARD / AGAIN / KEEP all just return home for now - there is
        // nowhere to keep or discard *to* until the memory card is wired in.
        if (touchPressed && buttonHit(BTN_V_DISCARD, tx, ty)) {
            logAction("DISCARD (touch)");
            goTo(SCR_READY);
        } else if (touchPressed && buttonHit(BTN_V_AGAIN, tx, ty)) {
            logAction("RUN AGAIN (touch)");
            goTo(SCR_LABEL);
        } else if (touchPressed && buttonHit(BTN_V_KEEP, tx, ty)) {
            logAction("KEEP (touch)");
            goTo(SCR_READY);
        } else if (!touchCalibrated && sinceEnter > AUTO_ADVANCE_MS + 2000) {
            logAction("back to home (timer - not calibrated)");
            goTo(SCR_READY);
        }
        break;
    }

    case SCR_STATS: {
        if (needsRepaint) { drawStats(tft, state, scanStats); needsRepaint = false; }

        if (touchPressed && buttonHit(BTN_STATS_RESET, tx, ty)) {
            // The history file is kept as /scans-old.csv rather than deleted,
            // so pressing this cannot destroy a record by accident.
            logAction("CLEAR COUNT (touch)");
            storageStatsReset(scanStats);
            storageLog("tally cleared - previous history kept as /scans-old.csv");
            needsRepaint = true;
        } else if (touchPressed && buttonHit(BTN_STATS_BACK, tx, ty)) {
            logAction("BACK (touch)");
            goTo(SCR_READY);
        } else if (!touchCalibrated && sinceEnter > AUTO_ADVANCE_MS) {
            goTo(SCR_READY);
        }
        break;
    }

    case SCR_MESSAGE: {
        if (needsRepaint) { drawMessage(tft, state); needsRepaint = false; }

        if (touchPressed && buttonHit(BTN_MSG_OK, tx, ty)) {
            logAction("OK (touch)");
            goTo(SCR_READY);
        } else if (!touchCalibrated && sinceEnter > AUTO_ADVANCE_MS) {
            goTo(SCR_READY);
        }
        break;
    }

    case SCR_TOUCH_SETUP: {
        if (needsRepaint) { drawTouchSetup(tft, state); needsRepaint = false; }

        if (touchPressed && buttonHit(BTN_TOUCH_START, tx, ty)) {
            logAction("touch calibration starting");
            runTouchCalibration();
            storageLog("touch calibration saved");
            snprintf(state.messageTitle, sizeof(state.messageTitle), "CALIBRATION SAVED");
            snprintf(state.messageBody,  sizeof(state.messageBody),
                     "New touch settings are active.");
            goTo(SCR_MESSAGE);
        }
        break;
    }
    }

    delay(10);
}
