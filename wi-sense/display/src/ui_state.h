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

// ---------------------------------------------------------------------------
// What the machine tells the person at the gate.
//
// Three levels, not two, and the middle one is the important one. A detector
// produces a confidence, never a yes or a no; forcing that into two buckets
// means the cases it is least sure about get reported as though it were
// certain. UNSURE is the machine admitting it does not know, which is the
// honest answer often enough to be worth a colour of its own.
// ---------------------------------------------------------------------------
enum Verdict : uint8_t {
    VERDICT_CLEAR = 0,   // green, steady   - the bag can go through
    VERDICT_UNSURE,      // orange, steady  - worth a look inside
    VERDICT_METAL        // red, flashing   - hand-search it
};

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
    SCR_TOUCH_SETUP,

    // --- everyday use at the gate ---
    //
    // These five are what the machine does when nobody is operating it. The
    // distance sensor drives all of it: a bag arriving is what starts a scan,
    // and nothing above needs pressing. The collection screens above are still
    // reachable, but behind a small button rather than in front of you.
    SCR_READY,          // nothing in the tray, waiting
    SCR_ARMING,         // something is there - must hold still before scanning
    SCR_GATE_SCAN,      // scanning a bag
    SCR_GATE_RESULT,    // showing the answer, waiting for the bag to leave
    SCR_ADMIN           // trigger settings, behind the SETUP button
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

    // --- everyday scanning at the gate ---
    //
    // The four settings are kept in the board's own flash and changed on the
    // SETUP screen, because the right numbers depend on where the sensor ends
    // up sitting relative to the tray. They cannot be guessed from a desk.
    uint16_t triggerMm;      // closer than this and a bag is considered present
    uint16_t rearmMm;        // must go back past this before the next scan
    uint16_t dwellMs;        // how long it must hold still before scanning
    uint16_t scanMs;         // how long one gate scan lasts
    uint32_t phaseMs;        // time spent on the current screen, for progress

    // The stand-in answer. It means NOTHING - see gateDecide() in ui_state.cpp.
    Verdict verdict;

    // How sure the detector is, 0-100. Today it is invented alongside the
    // verdict above; when a real detector exists this is what it produces, and
    // the two thresholds in theme.h are what turn it into one of the three
    // levels. Keeping the number here now means only its source has to change
    // later, not the screens.
    uint8_t confidence;

    // --- is there a trained detector yet? ---
    bool modelTrained;
};

// Picks the stand-in verdict. Deliberately not random: it cycles green,
// orange, red, so anyone watching can see it is not measuring anything.
void gateDecide(UiState& s);

// The word on the result screen, and the line under it saying what to do.
const char* verdictWord(Verdict v);
const char* verdictAdvice(Verdict v);

const char* categoryName(Category c);
const char* categoryFolder(Category c);

// --- stand-in data, replaced by real counters later ---
void simInit(UiState& s);
bool simSelfTest(UiState& s, uint32_t elapsedMs);   // true if something changed
void simStartRun(UiState& s);
void simRunTick(UiState& s, uint32_t dtMs);
void simFinishRun(UiState& s);
