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

// ---------------------------------------------------------------------------
// The running tally.
//
// Like the page count on a printer: how many bags this machine has looked at
// since the counter was last cleared, and how they came out. It lives on the
// card in two files, so it survives a power cycle and can be read on a laptop.
//
//   /stats.txt   the totals, as plain key=value lines
//   /scans.csv   one row per scan, in order - the full history
//
// Every row and the totals carry which detector produced them. While there is
// no trained detector that reads "stand-in", and it matters: these are counts
// of invented answers. Recording them unmarked would leave a file that later
// looks like evidence of something. When a real detector exists it writes its
// own name instead, and the two can be told apart without guesswork.
// ---------------------------------------------------------------------------

// Levels are the three the screen shows, in the same order as the Verdict
// enum: 0 clear, 1 unsure, 2 metal. Kept as a plain number here so that the
// card code does not need to know what a verdict is.
static const uint8_t STATS_LEVELS = 3;

struct ScanStats {
    bool     loaded;                  // the file was read (or created) cleanly
    uint32_t total;
    uint32_t byLevel[STATS_LEVELS];
    uint32_t standIn;                 // of the total, how many had no real detector
};

// Reads the tally, creating an empty one if this is a new card. Safe with no
// card: returns false and leaves the counts at zero.
bool storageStatsLoad(ScanStats& stats);

// Records one finished scan: bumps the totals, rewrites /stats.txt, and
// appends a row to /scans.csv. detector is the name of whatever produced the
// answer - "stand-in" while none is trained.
bool storageStatsRecord(ScanStats& stats, uint8_t level, uint8_t confidence,
                        uint16_t distanceMm, const char* detector);

// Clears the totals and starts a new history file, keeping the old one as
// /scans-old.csv so a clear cannot silently destroy a record.
bool storageStatsReset(ScanStats& stats);
