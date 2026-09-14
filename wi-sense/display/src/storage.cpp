#include <Arduino.h>
#include <string.h>

#include "storage.h"
#include "board_pins.h"

static const char* TEST_PATH   = "/wisense_check.txt";
static const char* TEST_MARKER = "wi-sense card check";
static const char* LOG_PATH    = "/log.txt";
static const char* STATS_PATH  = "/stats.txt";
static const char* SCANS_PATH  = "/scans.csv";
static const char* SCANS_OLD   = "/scans-old.csv";

// Folders captures will eventually be written into. Created now so that any
// permission or naming problem shows up during a hardware check rather than
// in the middle of collecting data.
static const char* FOLDERS[] = {
    "/data",
    "/data/empty",
    "/data/metal",
    "/data/non_metal",
    "/data/other",
    "/data/archive"
};

static void setError(CardInfo& info, const char* why) {
    strncpy(info.error, why, sizeof(info.error) - 1);
    info.error[sizeof(info.error) - 1] = '\0';
}

#ifdef BOARD_S3
// ---------------------------------------------------------------------------
// The S3 bench board's onboard reader, over SD/MMC in 1-wire mode (three
// pins: CLK, CMD, D0 - see board_pins.h). Unlike the original ESP32, the S3
// routes these through a GPIO matrix rather than fixed pins, which is why
// setPins() below is required rather than optional.
// ---------------------------------------------------------------------------
#include <FS.h>
#include <SD_MMC.h>

static bool s_mounted = false;

bool storageBegin(CardInfo& info) {
    memset(&info, 0, sizeof(info));
    strncpy(info.type, "none", sizeof(info.type) - 1);
    s_mounted = false;

    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);

    // true = 1-wire mode, matching setPins() above (no D1/D2/D3 given).
    if (!SD_MMC.begin("/sdcard", true)) {
        setError(info, "card did not answer");
        return false;
    }

    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        setError(info, "no card in the slot");
        return false;
    }

    switch (cardType) {
        case CARD_MMC:  strncpy(info.type, "MMC",  sizeof(info.type) - 1); break;
        case CARD_SD:   strncpy(info.type, "SD",   sizeof(info.type) - 1); break;
        case CARD_SDHC: strncpy(info.type, "SDHC", sizeof(info.type) - 1); break;
        default:        strncpy(info.type, "?",    sizeof(info.type) - 1); break;
    }

    uint64_t total = SD_MMC.totalBytes();
    uint64_t used  = SD_MMC.usedBytes();
    info.totalMB = (uint32_t)(total / (1024ULL * 1024ULL));
    info.freeMB  = (uint32_t)((total > used ? total - used : 0) / (1024ULL * 1024ULL));
    info.mounted = true;
    s_mounted = true;
    return true;
}

bool storageSelfTest(CardInfo& info) {
    if (!info.mounted) {
        setError(info, "card not mounted");
        return false;
    }

    // --- write ---
    if (SD_MMC.exists(TEST_PATH)) SD_MMC.remove(TEST_PATH);

    File out = SD_MMC.open(TEST_PATH, FILE_WRITE);
    if (!out) {
        setError(info, "could not create a file");
        return false;
    }
    out.println(TEST_MARKER);
    out.close();
    info.writeOk = true;

    // --- read back ---
    File in = SD_MMC.open(TEST_PATH, FILE_READ);
    if (!in) {
        setError(info, "could not reopen the file");
        return false;
    }
    String line = in.readStringUntil('\n');
    in.close();
    line.trim();

    if (line != TEST_MARKER) {
        setError(info, "file read back wrong");
        return false;
    }
    info.readBackOk = true;

    // --- folders ---
    for (unsigned i = 0; i < sizeof(FOLDERS) / sizeof(FOLDERS[0]); i++) {
        if (!SD_MMC.exists(FOLDERS[i]) && !SD_MMC.mkdir(FOLDERS[i])) {
            setError(info, "could not make the data folders");
            return false;
        }
    }
    info.foldersOk = true;

    return true;
}

bool storageLog(const char* line) {
    if (!s_mounted || !line) return false;

    File f = SD_MMC.open(LOG_PATH, FILE_APPEND);
    if (!f) return false;

    f.print(millis());
    f.print("ms  ");
    f.println(line);
    f.close();
    return true;
}

// --- the running tally ------------------------------------------------------
//
// /stats.txt is plain key=value lines rather than JSON, because it has to be
// written by hand here and read by eye on a laptop. Nothing needs a parser.

static void statsZero(ScanStats& stats) {
    stats.total   = 0;
    stats.standIn = 0;
    for (uint8_t i = 0; i < STATS_LEVELS; i++) stats.byLevel[i] = 0;
}

static bool statsWriteTotals(const ScanStats& stats) {
    File f = SD_MMC.open(STATS_PATH, FILE_WRITE);   // truncates, which is wanted
    if (!f) return false;

    f.println("# wi-sense scan tally. Rewritten after every scan.");
    f.println("# 'standin' counts answers produced with no trained detector.");
    f.println("schema=1");
    f.print("total=");   f.println(stats.total);
    f.print("clear=");   f.println(stats.byLevel[0]);
    f.print("unsure=");  f.println(stats.byLevel[1]);
    f.print("metal=");   f.println(stats.byLevel[2]);
    f.print("standin="); f.println(stats.standIn);
    f.close();
    return true;
}

static uint32_t statsValueAfter(const String& line, const char* key) {
    String prefix = String(key) + "=";
    if (!line.startsWith(prefix)) return 0xFFFFFFFFUL;   // "not this key"
    return (uint32_t)line.substring(prefix.length()).toInt();
}

bool storageStatsLoad(ScanStats& stats) {
    memset(&stats, 0, sizeof(stats));
    if (!s_mounted) return false;

    if (!SD_MMC.exists(STATS_PATH)) {
        // A new card. Start the tally and the history so that the very first
        // scan appends to something that already exists.
        statsZero(stats);
        if (!statsWriteTotals(stats)) return false;

        File f = SD_MMC.open(SCANS_PATH, FILE_WRITE);
        if (!f) return false;
        f.println("index,uptime_ms,level,confidence,distance_mm,detector");
        f.close();

        stats.loaded = true;
        return true;
    }

    File f = SD_MMC.open(STATS_PATH, FILE_READ);
    if (!f) return false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;

        uint32_t v;
        if ((v = statsValueAfter(line, "total"))   != 0xFFFFFFFFUL) stats.total      = v;
        else if ((v = statsValueAfter(line, "clear"))   != 0xFFFFFFFFUL) stats.byLevel[0] = v;
        else if ((v = statsValueAfter(line, "unsure"))  != 0xFFFFFFFFUL) stats.byLevel[1] = v;
        else if ((v = statsValueAfter(line, "metal"))   != 0xFFFFFFFFUL) stats.byLevel[2] = v;
        else if ((v = statsValueAfter(line, "standin")) != 0xFFFFFFFFUL) stats.standIn    = v;
    }
    f.close();
    stats.loaded = true;
    return true;
}

bool storageStatsRecord(ScanStats& stats, uint8_t level, uint8_t confidence,
                        uint16_t distanceMm, const char* detector) {
    if (level >= STATS_LEVELS) return false;

    // The counts are kept in memory whether or not the card is there, so the
    // screen still shows this session honestly on a machine with no card. Only
    // the writing below needs the card.
    stats.total++;
    stats.byLevel[level]++;
    if (detector && strcmp(detector, "stand-in") == 0) stats.standIn++;

    if (!s_mounted) return false;

    File f = SD_MMC.open(SCANS_PATH, FILE_APPEND);
    if (f) {
        f.print(stats.total);      f.print(',');
        f.print(millis());         f.print(',');
        f.print(level);            f.print(',');
        f.print(confidence);       f.print(',');
        f.print(distanceMm);       f.print(',');
        f.println(detector ? detector : "unknown");
        f.close();
    }
    return statsWriteTotals(stats);
}

bool storageStatsReset(ScanStats& stats) {
    statsZero(stats);
    if (!s_mounted) return false;

    // The old history is kept rather than deleted. Clearing a counter should
    // not be able to destroy a record by accident.
    if (SD_MMC.exists(SCANS_PATH)) {
        if (SD_MMC.exists(SCANS_OLD)) SD_MMC.remove(SCANS_OLD);
        SD_MMC.rename(SCANS_PATH, SCANS_OLD);
    }

    File f = SD_MMC.open(SCANS_PATH, FILE_WRITE);
    if (f) {
        f.println("index,uptime_ms,level,confidence,distance_mm,detector");
        f.close();
    }
    return statsWriteTotals(stats);
}

#else
// ---------------------------------------------------------------------------
// Classic ESP32-WROOM-32: no onboard reader exists on this board. Reported
// honestly as unsupported rather than silently doing nothing, so a mistaken
// call here is obvious in the serial log rather than a quiet no-op. The
// back-panel slot (docs/sd-card-plan.md) is the option for this board, and
// would need its own implementation here when that is picked up.
// ---------------------------------------------------------------------------
bool storageBegin(CardInfo& info) {
    memset(&info, 0, sizeof(info));
    strncpy(info.type, "none", sizeof(info.type) - 1);
    setError(info, "no onboard reader on this board");
    return false;
}

bool storageSelfTest(CardInfo& info) {
    setError(info, "no onboard reader on this board");
    return false;
}

bool storageLog(const char* /*line*/) {
    return false;
}

bool storageStatsLoad(ScanStats& stats) {
    memset(&stats, 0, sizeof(stats));
    return false;
}

// The counts are still kept in memory, so the screen tells the truth about
// this session even on a board with nowhere to write it down.
bool storageStatsRecord(ScanStats& stats, uint8_t level, uint8_t /*confidence*/,
                        uint16_t /*distanceMm*/, const char* detector) {
    if (level >= STATS_LEVELS) return false;
    stats.total++;
    stats.byLevel[level]++;
    if (detector && strcmp(detector, "stand-in") == 0) stats.standIn++;
    return false;
}

bool storageStatsReset(ScanStats& stats) {
    memset(&stats, 0, sizeof(stats));
    return false;
}

#endif
