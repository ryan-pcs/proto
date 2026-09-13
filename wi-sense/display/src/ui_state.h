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
    SCR_MESSAGE
};

struct UiState {
    // --- start-up checks ---
    bool receiverReady;
    bool wifiLinked;
    bool transmitterReady;
    bool cardPresent;
    uint8_t checksPassed;      // 0..4, used to know when to redraw

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
