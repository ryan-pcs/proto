#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// THIS FILE IS THE SEAM.
//
// Everything the screen can display lives in UiState below. Nothing in the
// drawing code ever asks where a number came from — it only reads UiState.
//
// Today, UiState is filled by the sim* functions at the bottom, which invent
// plausible numbers so the screens can be built and judged without any radio
// hardware attached.
//
// Later, on the receiver board, the same UiState is filled from real CSI
// counters instead. The drawing code does not change at all.
// ---------------------------------------------------------------------------

enum Category : uint8_t {
    CAT_EMPTY = 0,
    CAT_METAL,
    CAT_NON_METAL,
    CAT_OTHER
};

enum ScreenId : uint8_t {
    SCR_BOOT = 0,
    SCR_HOME,
    SCR_LABEL,
    SCR_SETTINGS,
    SCR_SCANNING,
    SCR_VERDICT,
    SCR_MESSAGE,
    SCR_TOUCH_SETUP
};

struct UiState {
    // --- start-up checks ---
    bool receiverReady;
    bool wifiLinked;
    bool transmitterReady;
    bool cardPresent;
    uint8_t checksPassed;      // 0..4, used to know when to redraw

    // The card check is real, unlike the three above it. cardWorking is set
    // before the start-up screen appears; cardDetail is what to show beside it.
    bool cardWorking;
    char cardDetail[30];

    // The distance sensor is a real check too. distanceMm is refreshed on the
    // home screen so it can be watched while a hand is waved in front of it.
    bool     sensorWorking;
    bool     sensorPresent;
    bool     sensorInRange;
    uint16_t distanceMm;
    char     sensorDetail[30];

    // --- the run being set up ---
    Category category;
    char objectName[24];
    uint16_t durationS;
    uint16_t rateHz;

    // --- live numbers during a run ---
    uint32_t elapsedMs;
    uint32_t packetsSent;
    uint32_t rowsCaptured;
    uint32_t drops;
    uint32_t txFailures;
    float coverage;            // 0.0 .. 1.0+

    // --- result of the finished run ---
    bool runPassed;
    char failReason[56];

    // --- message screen ---
    char messageTitle[24];
    char messageBody[64];

    // --- is there a trained detector yet? ---
    bool modelTrained;
};

const char* categoryName(Category c);
const char* categoryFolder(Category c);

// --- stand-in data, replaced by real counters later ---
void simInit(UiState& s);
bool simSelfTest(UiState& s, uint32_t elapsedMs);   // true if something changed
void simStartRun(UiState& s);
void simRunTick(UiState& s, uint32_t dtMs);
void simFinishRun(UiState& s);
