#include <Arduino.h>
#include <string.h>
#include "ui_state.h"
#include "theme.h"

const char* categoryName(Category c) {
    switch (c) {
        case CAT_EMPTY:     return "EMPTY";
        case CAT_METAL:     return "METAL";
        case CAT_NON_METAL: return "NON-METAL";
        default:            return "OTHER";
    }
}

const char* categoryFolder(Category c) {
    switch (c) {
        case CAT_EMPTY:     return "empty";
        case CAT_METAL:     return "metal";
        case CAT_NON_METAL: return "non_metal";
        default:            return "other";
    }
}

void simInit(UiState& s) {
    memset(&s, 0, sizeof(s));
    s.category   = CAT_EMPTY;
    strncpy(s.objectName, "none", sizeof(s.objectName) - 1);
    s.durationS  = 5;
    s.rateHz     = 20;
    s.modelTrained = false;      // stays false until a real model exists

    // Starting points only. Every one of these is a guess until the sensor is
    // mounted and a real bag is put in front of it; the SETUP screen exists so
    // they can be found by trying rather than by arguing.
    s.triggerMm = GATE_TRIGGER_MM_DEFAULT;
    s.rearmMm   = GATE_TRIGGER_MM_DEFAULT + GATE_REARM_GAP_MM;
    s.dwellMs   = GATE_DWELL_MS_DEFAULT;
    s.scanMs    = GATE_SCAN_MS_DEFAULT;
}

// The stand-in verdict.
//
// This cycles green, orange, red, green... It is not random, and it is not a
// measurement of anything - a random answer would look like a decision, and
// someone would eventually believe it. A fixed cycle is obviously mechanical
// to anyone who watches it more than twice.
//
// The shape of it is right even though the answer is not: a real detector will
// produce the confidence, and the same two thresholds will turn it into the
// same three levels. So when that day comes, only the first half of this
// function is replaced - the screens and the thresholds stay as they are.
//
// The invented confidences below sit in the middle of each band on purpose, so
// that nothing depends on exactly where the thresholds are while they are
// still placeholders.
void gateDecide(UiState& s) {
    switch (s.verdict) {
        case VERDICT_CLEAR:  s.verdict = VERDICT_UNSURE; s.confidence = (GATE_CONF_UNSURE + GATE_CONF_METAL) / 2; break;
        case VERDICT_UNSURE: s.verdict = VERDICT_METAL;  s.confidence = (GATE_CONF_METAL + 100) / 2;              break;
        default:             s.verdict = VERDICT_CLEAR;  s.confidence = GATE_CONF_UNSURE / 2;                     break;
    }
}

const char* verdictWord(Verdict v) {
    switch (v) {
        case VERDICT_CLEAR:  return "CLEAR";
        case VERDICT_UNSURE: return "CHECK";
        default:             return "SEARCH";
    }
}

const char* verdictAdvice(Verdict v) {
    switch (v) {
        case VERDICT_CLEAR:  return "bag can go through";
        case VERDICT_UNSURE: return "not sure - look inside before it goes";
        default:             return "hand-search this bag now";
    }
}

// Start-up checks tick on one at a time so the screen has something to show.
bool simSelfTest(UiState& s, uint32_t elapsedMs) {
    uint8_t before = s.checksPassed;

    s.receiverReady    = elapsedMs > 400;
    s.wifiLinked       = elapsedMs > 900;
    s.transmitterReady = elapsedMs > 1500;

    // This one is not invented. cardWorking comes from the real card check
    // that ran before this screen appeared; the delay is only so the four
    // lines appear in order rather than all at once.
    s.cardPresent      = (elapsedMs > 2000) && s.cardWorking;
    s.sensorPresent    = (elapsedMs > 2400) && s.sensorWorking;

    s.checksPassed = (uint8_t)s.receiverReady + (uint8_t)s.wifiLinked +
                     (uint8_t)s.transmitterReady + (uint8_t)s.cardPresent +
                     (uint8_t)s.sensorPresent;

    return s.checksPassed != before;
}

void simStartRun(UiState& s) {
    s.elapsedMs    = 0;
    s.packetsSent  = 0;
    s.rowsCaptured = 0;
    s.drops        = 0;
    s.txFailures   = 0;
    s.coverage     = 0.0f;
    s.runPassed    = false;
    s.failReason[0] = '\0';
}

void simRunTick(UiState& s, uint32_t dtMs) {
    s.elapsedMs += dtMs;

    // packets the transmitter would have sent by now
    s.packetsSent = (uint32_t)((uint64_t)s.elapsedMs * s.rateHz / 1000ULL);

    // Pretend we capture most of them, with a little jitter, so the coverage
    // figure moves around the way a real one does.
    uint32_t expected = s.packetsSent;
    if (expected > 0) {
        uint32_t missed = expected / 8;                 // roughly 88% coverage
        if (random(0, 100) < 3) missed += 2;            // occasional stumble
        s.rowsCaptured = (expected > missed) ? expected - missed : 0;
        s.coverage = (float)s.rowsCaptured / (float)expected;
    }
}

void simFinishRun(UiState& s) {
    if (s.coverage < COVERAGE_MIN) {
        s.runPassed = false;
        snprintf(s.failReason, sizeof(s.failReason),
                 "coverage %d%%, minimum is 75%%", (int)(s.coverage * 100.0f));
    } else if (s.drops > 0) {
        s.runPassed = false;
        snprintf(s.failReason, sizeof(s.failReason),
                 "%lu dropped reading(s)", (unsigned long)s.drops);
    } else if (s.txFailures > 0) {
        s.runPassed = false;
        snprintf(s.failReason, sizeof(s.failReason),
                 "%lu transmitter failure(s)", (unsigned long)s.txFailures);
    } else {
        s.runPassed = true;
        s.failReason[0] = '\0';
    }
}
