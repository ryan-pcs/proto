#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// The memory card.
//
// This talks to the card reader built into the S3 bench board itself - a
// different physical slot from the one on the back of the display panel that
// the earlier plan (docs/sd-card-plan.md) was written around. The upside of
// this one: it has its own dedicated pins, so it never has to share wires
// with the screen the way the back-panel slot would.
//
// The classic ESP32-WROOM-32 (the eventual receiver board) has no onboard
// reader - only the back-panel slot exists there. So this file only does
// anything on the S3 build; see the note in storage.cpp.
//
// Nothing here writes scan captures yet. That arrives when there are real
// readings. What exists now: mounting the card, a self-test that also makes
// the capture folders, and a simple log file anything in the program can
// append a line to.
// ---------------------------------------------------------------------------

struct CardInfo {
    bool     mounted;       // the card answered and was mounted
    bool     writeOk;       // a test file was created
    bool     readBackOk;    // that file read back with the right contents
    bool     foldersOk;     // the capture folders exist
    uint32_t totalMB;
    uint32_t freeMB;
    char     type[10];      // SD, SDHC, MMC
    char     error[48];     // why it failed, in plain words
};

// Mounts the card and fills in type and size. False if there is no card, the
// wiring is wrong, or (on a board with no onboard reader) this simply isn't
// supported - info.error says which.
bool storageBegin(CardInfo& info);

// Writes a file, reads it back, and makes the capture folders. Run after
// storageBegin. False on the first thing that fails, with the reason in
// info.error.
bool storageSelfTest(CardInfo& info);

// Appends one line to /log.txt on the card, with the time since boot in
// front of it. Safe to call whether or not the card is mounted - if it
// isn't, this just does nothing and returns false rather than freezing or
// crashing the program.
bool storageLog(const char* line);
