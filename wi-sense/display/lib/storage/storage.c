#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_console.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs.h"

#include "storage.h"
#include "serial_log.h"
#include "serial_cli.h"
#include "module_registry.h"

static const char *TAG = "storage";

#define STORAGE_DEFAULT_MOUNT_POINT "/sdcard"
#define STORAGE_DEFAULT_MAX_FILES   5
#define STORAGE_CAT_MAX_BYTES       4096

static const serial_log_iface_t *s_log;
static storage_config_t s_cfg;
static char s_mount_point[64];
static bool s_mounted;
static sdmmc_card_t *s_card;

// CLI-only navigation state (the storage_iface_t itself stays stateless —
// this cwd only affects relative paths typed at the console, not
// programmatic callers using the iface or plain stdio).
static char s_cwd[320];

/* ---------- iface implementation ---------- */

static const char *iface_get_mount_point(void)
{
    return s_mount_point;
}

static bool iface_is_mounted(void)
{
    return s_mounted;
}

static esp_err_t iface_get_space(uint64_t *total_bytes, uint64_t *free_bytes)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!total_bytes || !free_bytes) {
        return ESP_ERR_INVALID_ARG;
    }

    return esp_vfs_fat_info(s_mount_point, total_bytes, free_bytes);
}

static esp_err_t iface_exists(const char *path, bool *out_exists)
{
    if (!path || !out_exists) {
        return ESP_ERR_INVALID_ARG;
    }
    struct stat st;
    if (stat(path, &st) == 0) {
        *out_exists = true;
        return ESP_OK;
    }
    if (errno == ENOENT) {
        *out_exists = false;
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t iface_mkdir(const char *path)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mkdir(path, 0775) != 0) {
        s_log->error(TAG, "mkdir: failed on '%s': errno=%d", path, errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t iface_remove(const char *path)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    if (remove(path) != 0) {
        s_log->error(TAG, "remove: failed on '%s': errno=%d", path, errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t iface_list_dir(const char *path, storage_dirent_cb_t cb, void *user_ctx)
{
    if (!path || !cb) {
        return ESP_ERR_INVALID_ARG;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        return ESP_FAIL;
    }

    struct dirent *entry;
    char full_path[320];
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        bool is_dir = false;
        size_t size = 0;

        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(full_path, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            size = (size_t)st.st_size;
        }

        if (!cb(entry->d_name, is_dir, size, user_ctx)) {
            break;
        }
    }

    closedir(dir);
    return ESP_OK;
}

static storage_iface_t s_iface = {
    .get_mount_point = iface_get_mount_point,
    .is_mounted      = iface_is_mounted,
    .get_space       = iface_get_space,
    .exists          = iface_exists,
    .mkdir           = iface_mkdir,
    .remove          = iface_remove,
    .list_dir        = iface_list_dir,
};

/* ---------- path resolution (CLI only) ---------- */

// Resolves `input` against the current CLI working directory (s_cwd) into
// an absolute, "."/".."-collapsed path written into `out`. Absolute input
// (leading '/') is used as-is before collapsing. A NULL/empty input or
// "." resolves to the cwd itself. Refuses to resolve above the mount
// point — ".." past the root just clamps back to the mount point, the
// same way most shells clamp cd .. at "/".
// Returns false only on a malformed/oversized input.
static bool resolve_path(const char *input, char *out, size_t out_len)
{
    char combined[320];

    if (!input || input[0] == '\0' || strcmp(input, ".") == 0) {
        strncpy(combined, s_cwd, sizeof(combined) - 1);
        combined[sizeof(combined) - 1] = '\0';
    } else if (input[0] == '/') {
        if (strlen(input) >= sizeof(combined)) {
            return false;
        }
        strncpy(combined, input, sizeof(combined) - 1);
        combined[sizeof(combined) - 1] = '\0';
    } else {
        int n = snprintf(combined, sizeof(combined), "%s/%s", s_cwd, input);
        if (n < 0 || (size_t)n >= sizeof(combined)) {
            return false;
        }
    }

    // Tokenize on '/' and resolve "." / ".." components onto a stack of
    // segment pointers (into `work`, which we mutate in place — safe
    // since strtok_r only ever inserts '\0' at existing '/' positions).
    char work[320];
    strncpy(work, combined, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    char *segments[32];
    int seg_count = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(work, "/", &saveptr);
    while (tok) {
        if (strcmp(tok, ".") == 0) {
            // no-op
        } else if (strcmp(tok, "..") == 0) {
            if (seg_count > 0) {
                seg_count--;
            }
        } else if (seg_count < (int)(sizeof(segments) / sizeof(segments[0]))) {
            segments[seg_count++] = tok;
        }
        tok = strtok_r(NULL, "/", &saveptr);
    }

    char result[320] = "";
    for (int i = 0; i < seg_count; i++) {
        strncat(result, "/", sizeof(result) - strlen(result) - 1);
        strncat(result, segments[i], sizeof(result) - strlen(result) - 1);
    }
    if (result[0] == '\0') {
        // Collapsed all the way to (or past) the root — clamp to the
        // mount point rather than returning an empty path.
        strncpy(result, s_mount_point, sizeof(result) - 1);
        result[sizeof(result) - 1] = '\0';
    }

    if (strlen(result) >= out_len) {
        return false;
    }
    strncpy(out, result, out_len - 1);
    out[out_len - 1] = '\0';
    return true;
}

/* ---------- Persistent log sink ---------- */
//
// storage acts as serial_log's secondary/durable sink: WARN+ lines get
// mirrored to a preallocated, FIXED-SIZE circular file on the card. Fixed
// size matters twice over — the file's directory entry and FAT chain never
// change after creation, which means almost no FAT metadata churn (the
// actual thing that wears/corrupts SD cards under a small-append workload,
// far more than the raw byte count) and much better survival of a
// power-loss mid-write, since there's no size/length field that can end up
// inconsistent with the data.
//
// serial_log owns POLICY (what counts as a duplicate, the RTC last-words
// ring, level filtering). storage owns I/O (the file format, the flusher
// task, fsync timing). The sink callback below is the seam between them —
// it receives an already-formatted line and does nothing but queue it.

#define LOG_FILE_NAME       "/system.log"      // relative to s_mount_point
#define LOG_HEADER_MAGIC    0x4C4F4731u        // "LOG1"
#define LOG_SLOT_SIZE       128                // bytes per log line, NUL-padded
#define LOG_SLOT_COUNT      2048                // ~256K total file size
#define LOG_HEADER_SIZE     512                 // reserved header region (sector-friendly)
#define PENDING_RING_LINES  32                  // RAM staging ring, drained by the flusher

// How long an open dedup streak can sit before the flusher force-closes it
// even without a new, different line arriving to trigger the close — bounds
// how stale a "still repeating" summary can get on the card.
#define STREAK_MAX_OPEN_US  (10 * 1000 * 1000)

#define LOG_FLUSH_INTERVAL_MS 2000   // how often the flusher actually writes
#define LOG_FLUSHER_POLL_MS   200    // how often it checks for stop() — bounds
                                      // stop() latency independently of the
                                      // flush interval, same idiom espnow's
                                      // recv task uses for its own poll.

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t slot_size;
    uint32_t slot_count;
    uint32_t write_index;   // next slot to be written
    uint32_t wrap_count;    // informational: how many times the ring has wrapped
} log_file_header_t;

typedef struct {
    char text[LOG_SLOT_SIZE];
} pending_line_t;

static bool             s_log_sink_active = false;
static pending_line_t   s_pending[PENDING_RING_LINES];
static size_t            s_pending_head = 0;     // next write index
static size_t            s_pending_count = 0;    // currently queued, <= PENDING_RING_LINES
static uint32_t          s_dropped_count = 0;    // lines lost to a full ring or lock contention
static SemaphoreHandle_t s_pending_mutex = NULL;

// Dedup/rate-limit state — protected by s_pending_mutex, same as the ring.
static esp_log_level_t s_streak_level;
static char             s_streak_tag[16];
static char             s_streak_msg[100];
static uint32_t         s_streak_count = 0;   // 0 == no open streak
static int64_t          s_streak_first_us = 0;
static int64_t          s_streak_last_us = 0;

static TaskHandle_t      s_flusher_task = NULL;
static volatile bool     s_flusher_run = false;
static SemaphoreHandle_t s_flusher_exited = NULL;

// Set only by the flusher task around its own internal error reporting, to
// break the recursion a naive implementation would hit: a failed write
// logs via s_log->error(), which dispatches back into storage_log_sink(),
// which would otherwise try to queue a line about the very write that just
// failed. There's a narrow window where a concurrent, UNRELATED log call
// from another task could also be dropped by this guard — accepted as a
// negligible, best-effort cost rather than adding a heavier lock for it.
static volatile bool s_recursion_guard = false;

static log_file_header_t s_file_hdr;
static bool s_log_file_ready = false;

static void format_line(char *out, size_t out_len, esp_log_level_t level,
                         const char *tag, const char *msg)
{
    static const char letters[] = { ' ', 'E', 'W', 'I', 'D', 'V' };
    char letter = (level >= 0 && (size_t)level < sizeof(letters)) ? letters[level] : '?';
    snprintf(out, out_len, "%lu [%c] %s: %s",
             (unsigned long)esp_timer_get_time() / 1000, letter, tag ? tag : "", msg ? msg : "");
}

// Caller must already hold s_pending_mutex. A true circular buffer: once
// full, writing at s_pending_head overwrites the oldest still-unflushed
// entry, which is exactly what "dropped" means here.
static void pending_push(const char *text)
{
    strncpy(s_pending[s_pending_head].text, text, LOG_SLOT_SIZE - 1);
    s_pending[s_pending_head].text[LOG_SLOT_SIZE - 1] = '\0';
    s_pending_head = (s_pending_head + 1) % PENDING_RING_LINES;
    if (s_pending_count < PENDING_RING_LINES) {
        s_pending_count++;
    } else {
        s_dropped_count++;
    }
}

// Caller must already hold s_pending_mutex. Turns the currently-open
// dedup streak (if any) into one queued line — either the line as-is (a
// streak of exactly 1 is just a normal line) or a "(xN in Ts)" summary —
// and clears streak state.
static void streak_close_locked(void)
{
    if (s_streak_count == 0) {
        return;
    }
    char line[LOG_SLOT_SIZE];
    if (s_streak_count == 1) {
        format_line(line, sizeof(line), s_streak_level, s_streak_tag, s_streak_msg);
    } else {
        long long span_s = (s_streak_last_us - s_streak_first_us) / 1000000;
        // Sized for the compiler's worst-case %lu/%lld width, not the
        // realistic one — format_line() below truncates the final,
        // combined line to LOG_SLOT_SIZE regardless.
        char summarized[192];
        snprintf(summarized, sizeof(summarized), "%s (x%lu in %llds)",
                 s_streak_msg, (unsigned long)s_streak_count, span_s);
        format_line(line, sizeof(line), s_streak_level, s_streak_tag, summarized);
    }
    pending_push(line);
    s_streak_count = 0;
}

// The serial_log sink callback itself. Called synchronously from whatever
// task originally logged the line — per the contract on
// serial_log_sink_fn_t, this MUST NOT block and MUST NOT log through
// s_log. Both are upheld here: the mutex is taken with a zero timeout
// (drop-and-count rather than wait), and internal errors — there are none
// on this path, but see ensure_log_file()/flush_pending_to_file() — use
// raw ESP_LOGx gated by s_recursion_guard.
static void storage_log_sink(esp_log_level_t level, const char *tag, const char *msg, void *ctx)
{
    (void)ctx;
    if (s_recursion_guard || !s_pending_mutex) {
        return;
    }
    if (xSemaphoreTake(s_pending_mutex, 0) != pdTRUE) {
        s_dropped_count++;
        return;
    }

    int64_t now_us = esp_timer_get_time();
    bool same_as_streak = s_streak_count > 0 && level == s_streak_level &&
                           strcmp(tag ? tag : "", s_streak_tag) == 0 &&
                           strcmp(msg ? msg : "", s_streak_msg) == 0;

    if (same_as_streak) {
        s_streak_count++;
        s_streak_last_us = now_us;
    } else {
        streak_close_locked();
        s_streak_level = level;
        strncpy(s_streak_tag, tag ? tag : "", sizeof(s_streak_tag) - 1);
        s_streak_tag[sizeof(s_streak_tag) - 1] = '\0';
        strncpy(s_streak_msg, msg ? msg : "", sizeof(s_streak_msg) - 1);
        s_streak_msg[sizeof(s_streak_msg) - 1] = '\0';
        s_streak_count = 1;
        s_streak_first_us = now_us;
        s_streak_last_us = now_us;
    }

    xSemaphoreGive(s_pending_mutex);
}

// Opens (or creates, at the fixed total size, once) the circular log file
// and loads its header into s_file_hdr. Only ever called from the flusher
// task, never from the sink callback — this does real, possibly slow I/O.
static bool ensure_log_file(void)
{
    if (s_log_file_ready) {
        return true;
    }
    if (!s_mounted) {
        return false;
    }

    char path[80];
    snprintf(path, sizeof(path), "%s%s", s_mount_point, LOG_FILE_NAME);

    struct stat st;
    bool exists_right_size = (stat(path, &st) == 0) &&
        ((uint32_t)st.st_size == (uint32_t)(LOG_HEADER_SIZE + (size_t)LOG_SLOT_SIZE * LOG_SLOT_COUNT));

    if (exists_right_size) {
        FILE *f = fopen(path, "r+b");
        if (!f) {
            ESP_LOGE(TAG, "log: failed to open existing %s", path);
            return false;
        }
        if (fread(&s_file_hdr, sizeof(s_file_hdr), 1, f) != 1 || s_file_hdr.magic != LOG_HEADER_MAGIC) {
            ESP_LOGW(TAG, "log: header in %s looks corrupt, reinitializing", path);
            fclose(f);
            exists_right_size = false;
        } else {
            fclose(f);
        }
    }

    if (!exists_right_size) {
        // (Re)create at the fixed total size, fully preallocated up front,
        // so the file's size — and therefore its FAT directory entry and
        // chain — never changes again for the rest of its life.
        FILE *f = fopen(path, "wb");
        if (!f) {
            ESP_LOGE(TAG, "log: failed to create %s", path);
            return false;
        }
        s_file_hdr = (log_file_header_t){
            .magic = LOG_HEADER_MAGIC,
            .version = 1,
            .slot_size = LOG_SLOT_SIZE,
            .slot_count = LOG_SLOT_COUNT,
            .write_index = 0,
            .wrap_count = 0,
        };
        fwrite(&s_file_hdr, sizeof(s_file_hdr), 1, f);

        static const char zeros[LOG_SLOT_SIZE] = {0};
        size_t pad = LOG_HEADER_SIZE - sizeof(s_file_hdr);
        while (pad > 0) {
            size_t chunk = pad < sizeof(zeros) ? pad : sizeof(zeros);
            fwrite(zeros, 1, chunk, f);
            pad -= chunk;
        }
        for (uint32_t i = 0; i < LOG_SLOT_COUNT; i++) {
            fwrite(zeros, 1, LOG_SLOT_SIZE, f);
        }

        fflush(f);
        int fd = fileno(f);
        if (fd >= 0) {
            fsync(fd);
        }
        fclose(f);
        s_log->info(TAG, "log: created %s (%lu slots, %lu bytes total)",
                    path, (unsigned long)LOG_SLOT_COUNT,
                    (unsigned long)(LOG_HEADER_SIZE + (size_t)LOG_SLOT_SIZE * LOG_SLOT_COUNT));
    }

    s_log_file_ready = true;
    return true;
}

// Drains whatever's currently queued in s_pending into the circular file
// as one batch: one open, N slot writes, ONE header update, one fsync,
// one close — this is the actual wear-reduction lever versus writing (and
// fsyncing) per line. Only ever called from the flusher task.
static void flush_pending_to_file(void)
{
    if (!s_mounted) {
        return;
    }

    // Static, not stack-local: PENDING_RING_LINES * LOG_SLOT_SIZE is 4KB,
    // which blew the flusher task's entire stack on its own (a heap/stack
    // corruption an ESP-IDF crash surfaces as an unrelated-looking
    // "xQueueSemaphoreTake ... uxItemSize == 0" assert, not a stack-guard
    // page fault). Safe as a single shared buffer because this function
    // has exactly one caller — log_flusher_task — never running
    // concurrently with itself.
    static pending_line_t local[PENDING_RING_LINES];
    size_t n = 0;

    if (xSemaphoreTake(s_pending_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_streak_count > 0 && (esp_timer_get_time() - s_streak_first_us) >= STREAK_MAX_OPEN_US) {
            streak_close_locked();
        }
        n = s_pending_count;
        if (n > 0) {
            size_t start = (s_pending_head + PENDING_RING_LINES - n) % PENDING_RING_LINES;
            for (size_t i = 0; i < n; i++) {
                local[i] = s_pending[(start + i) % PENDING_RING_LINES];
            }
            s_pending_count = 0;
        }
        xSemaphoreGive(s_pending_mutex);
    }

    if (n == 0) {
        return;
    }
    if (!ensure_log_file()) {
        return;   // lines already left the RAM ring; best-effort, they're gone
    }

    char path[80];
    snprintf(path, sizeof(path), "%s%s", s_mount_point, LOG_FILE_NAME);
    FILE *f = fopen(path, "r+b");
    if (!f) {
        s_recursion_guard = true;
        s_log->error(TAG, "log: failed to reopen %s for writing", path);
        s_recursion_guard = false;
        return;
    }

    char slot_buf[LOG_SLOT_SIZE];
    for (size_t i = 0; i < n; i++) {
        memset(slot_buf, 0, sizeof(slot_buf));
        strncpy(slot_buf, local[i].text, LOG_SLOT_SIZE - 1);

        long offset = (long)LOG_HEADER_SIZE + (long)s_file_hdr.write_index * LOG_SLOT_SIZE;
        fseek(f, offset, SEEK_SET);
        fwrite(slot_buf, 1, LOG_SLOT_SIZE, f);

        s_file_hdr.write_index++;
        if (s_file_hdr.write_index >= LOG_SLOT_COUNT) {
            s_file_hdr.write_index = 0;
            s_file_hdr.wrap_count++;
        }
    }

    fseek(f, 0, SEEK_SET);
    fwrite(&s_file_hdr, sizeof(s_file_hdr), 1, f);

    fflush(f);
    int fd = fileno(f);
    if (fd >= 0) {
        fsync(fd);   // durability boundary — see docs/supervisor.md on power loss
    }
    fclose(f);
}

static void log_flusher_task(void *arg)
{
    (void)arg;
    TickType_t last_flush = xTaskGetTickCount();
    const TickType_t flush_period = pdMS_TO_TICKS(LOG_FLUSH_INTERVAL_MS);

    while (s_flusher_run) {
        vTaskDelay(pdMS_TO_TICKS(LOG_FLUSHER_POLL_MS));
        if ((xTaskGetTickCount() - last_flush) >= flush_period) {
            flush_pending_to_file();
            last_flush = xTaskGetTickCount();
        }
    }
    flush_pending_to_file();   // best-effort final drain on the way out
    xSemaphoreGive(s_flusher_exited);
    vTaskDelete(NULL);
}

// Signals the flusher to exit and WAITS for it to actually confirm before
// returning — unlike espnow's recv task (which just nulls the handle and
// hopes), so a future restart can't spawn a second flusher on top of one
// that hasn't really exited yet.
static void stop_log_flusher(void)
{
    if (!s_flusher_task) {
        return;
    }
    s_flusher_run = false;
    if (s_flusher_exited) {
        if (xSemaphoreTake(s_flusher_exited, pdMS_TO_TICKS(LOG_FLUSHER_POLL_MS * 5)) != pdTRUE) {
            s_log->error(TAG, "log flusher did not confirm exit in time — leaking its task handle");
        }
    }
    s_flusher_task = NULL;
}

// If the previous boot ended in a crash (panic/watchdog, not a clean
// restart), pulls its last-words lines out of serial_log's RTC ring and
// queues them onto the card with a header line — so the SD log carries
// forward exactly the context a panic would otherwise erase along with
// the rest of RAM. Called once from mod_init(), after the mutex exists,
// before the flusher task is running — safe to touch s_pending directly
// without contention since nothing else can be logging through this sink
// yet (the sink isn't registered until after this returns).
static void recover_previous_boot_log(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    bool crash_like = (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
                        reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT);
    if (!crash_like || !s_log->get_previous_boot_log) {
        return;
    }

    serial_log_prev_line_t lines[24];
    size_t n = s_log->get_previous_boot_log(lines, sizeof(lines) / sizeof(lines[0]));
    if (n == 0) {
        return;
    }

    char header[LOG_SLOT_SIZE];
    snprintf(header, sizeof(header),
             "=== recovered after reset_reason=%d: %u line(s) from the crashed boot ===",
             (int)reason, (unsigned)n);

    xSemaphoreTake(s_pending_mutex, portMAX_DELAY);
    pending_push(header);
    for (size_t i = 0; i < n; i++) {
        pending_push(lines[i].text);
    }
    xSemaphoreGive(s_pending_mutex);

    s_log->warn(TAG, "recovered %u log line(s) from a crashed previous boot (reset_reason=%d)",
                (unsigned)n, (int)reason);
}

/* ---------- Persisted log level (see docs/storage.md) ---------- */
//
// Only the threshold itself is persisted — not log_sink_disable, and not
// whether a sink registered at all. `storage log level` is the only thing
// that ever writes this; storage_config_t.log_sink_min_level remains the
// boot-time default a persisted value overrides, not the other way round.

#define STORAGE_NVS_NAMESPACE    "storage"
#define STORAGE_NVS_KEY_LOG_LVL  "log_level"

// Returns the persisted level if one exists and is in range, otherwise
// `fallback` unchanged — covers "never set" and "NVS unavailable" the same
// way, since neither is actually an error worth surfacing here.
static esp_log_level_t load_persisted_log_level(esp_log_level_t fallback)
{
    nvs_handle_t h;
    if (nvs_open(STORAGE_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return fallback;
    }
    uint8_t v = 0;
    esp_err_t err = nvs_get_u8(h, STORAGE_NVS_KEY_LOG_LVL, &v);
    nvs_close(h);
    if (err != ESP_OK || v < ESP_LOG_ERROR || v > ESP_LOG_DEBUG) {
        return fallback;
    }
    return (esp_log_level_t)v;
}

// Best-effort — a failure to persist doesn't undo the live level change
// `cmd_storage_log_level()` already applied, just logs a warning.
static void persist_log_level(esp_log_level_t level)
{
    nvs_handle_t h;
    if (nvs_open(STORAGE_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        s_log->warn(TAG, "log level: could not open NVS to persist level=%d", (int)level);
        return;
    }
    nvs_set_u8(h, STORAGE_NVS_KEY_LOG_LVL, (uint8_t)level);
    nvs_commit(h);
    nvs_close(h);
}

/* ---------- CLI commands ---------- */

static int cmd_storage_info(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("mount_point=%s mounted=%s cwd=%s\n", s_mount_point, s_mounted ? "yes" : "no", s_cwd);

    if (s_mounted) {
        uint64_t total = 0, free_b = 0;
        if (iface_get_space(&total, &free_b) == ESP_OK) {
            printf("total=%llu bytes free=%llu bytes\n",
                   (unsigned long long)total, (unsigned long long)free_b);
        }
    }
    return 0;
}

// One line per entry, name and size on the *same* line (padded/aligned
// with printf field widths rather than wrapping the size onto its own
// line) — much easier to scan a directory with many files this way.
static bool ls_print_cb(const char *name, bool is_dir, size_t size, void *user_ctx)
{
    (void)user_ctx;
    if (is_dir) {
        printf("%-28s %10s\n", name, "<DIR>");
    } else {
        printf("%-28s %8u bytes\n", name, (unsigned int)size);
    }
    return true;
}

// argv[1], if present, is a raw (possibly relative) path; no argv[1] means
// "list the current directory".
static int cmd_storage_ls(int argc, char **argv)
{
    char path[320];
    if (argc < 2) {
        strncpy(path, s_cwd, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else if (!resolve_path(argv[1], path, sizeof(path))) {
        printf("FAILED (invalid path)\n");
        return 1;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        printf("FAILED (not found)\n");
        return 1;
    }
    if (!S_ISDIR(st.st_mode)) {
        printf("NOT A DIRECTORY\n");
        return 1;
    }

    printf("%s:\n", path);
    esp_err_t err = iface_list_dir(path, ls_print_cb, NULL);
    if (err != ESP_OK) {
        printf("FAILED (%s)\n", esp_err_to_name(err));
        return 1;
    }
    return 0;
}

// Changes the CLI's current working directory. No argument goes back to
// the mount point (like a shell's bare `cd`). Only affects how *this
// module's CLI commands* resolve relative paths — plain fopen() calls
// elsewhere in the program are unaffected, since there's no real process
// cwd on ESP-IDF.
static int cmd_storage_cd(int argc, char **argv)
{
    char path[320];

    if (argc < 2) {
        strncpy(s_cwd, s_mount_point, sizeof(s_cwd) - 1);
        s_cwd[sizeof(s_cwd) - 1] = '\0';
        printf("%s\n", s_cwd);
        return 0;
    }

    if (!resolve_path(argv[1], path, sizeof(path))) {
        printf("FAILED (invalid path)\n");
        return 1;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        printf("FAILED (not found)\n");
        return 1;
    }
    if (!S_ISDIR(st.st_mode)) {
        printf("NOT A DIRECTORY\n");
        return 1;
    }

    strncpy(s_cwd, path, sizeof(s_cwd) - 1);
    s_cwd[sizeof(s_cwd) - 1] = '\0';
    printf("%s\n", s_cwd);
    return 0;
}

static int cmd_storage_cat(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: storage cat <path>\n");
        return 1;
    }

    char path[320];
    if (!resolve_path(argv[1], path, sizeof(path))) {
        printf("FAILED (invalid path)\n");
        return 1;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        printf("FAILED (not found)\n");
        return 1;
    }
    if (st.st_size > STORAGE_CAT_MAX_BYTES) {
        printf("TOO LARGE (%ld bytes, limit %d)\n", (long)st.st_size, STORAGE_CAT_MAX_BYTES);
        return 1;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        printf("FAILED (could not open)\n");
        return 1;
    }

    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        fwrite(buf, 1, n, stdout);
    }
    fclose(f);
    printf("\n");
    return 0;
}

static int cmd_storage_write(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: storage write <path> <text...>\n");
        return 1;
    }

    char path[320];
    if (!resolve_path(argv[1], path, sizeof(path))) {
        printf("FAILED (invalid path)\n");
        return 1;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        printf("FAILED (could not open)\n");
        return 1;
    }

    for (int i = 2; i < argc; i++) {
        fputs(argv[i], f);
        if (i < argc - 1) {
            fputc(' ', f);
        }
    }
    fputc('\n', f);
    fclose(f);

    printf("OK\n");
    return 0;
}

static int cmd_storage_rm(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: storage rm <path>\n");
        return 1;
    }

    char path[320];
    if (!resolve_path(argv[1], path, sizeof(path))) {
        printf("FAILED (invalid path)\n");
        return 1;
    }

    esp_err_t err = iface_remove(path);
    if (err != ESP_OK) {
        printf("FAILED (%s)\n", esp_err_to_name(err));
        return 1;
    }
    printf("OK\n");
    return 0;
}

static int cmd_storage_mkdir(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: storage mkdir <path>\n");
        return 1;
    }

    char path[320];
    if (!resolve_path(argv[1], path, sizeof(path))) {
        printf("FAILED (invalid path)\n");
        return 1;
    }

    esp_err_t err = iface_mkdir(path);
    if (err != ESP_OK) {
        printf("FAILED (%s)\n", esp_err_to_name(err));
        return 1;
    }
    printf("OK\n");
    return 0;
}

static int cmd_storage_log_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    esp_log_level_t min_level = s_cfg.log_sink_min_level ? s_cfg.log_sink_min_level : ESP_LOG_WARN;
    printf("sink=%s min_level=%d(1=E 2=W 3=I 4=D) pending=%u dropped=%u flusher=%s\n",
           s_log_sink_active ? "active" : "inactive", (int)min_level,
           (unsigned)s_pending_count, (unsigned)s_dropped_count,
           s_flusher_task ? "running" : "stopped");
    if (s_log_file_ready) {
        printf("file: %s%s  slots=%lu write_index=%lu wrap_count=%lu (%s)\n",
               s_mount_point, LOG_FILE_NAME,
               (unsigned long)s_file_hdr.slot_count, (unsigned long)s_file_hdr.write_index,
               (unsigned long)s_file_hdr.wrap_count,
               s_file_hdr.wrap_count > 0 ? "full ring, oldest entries being overwritten" : "not yet wrapped");
    } else {
        printf("file: not yet created (nothing flushed since mount)\n");
    }
    return 0;
}

// Prints the last N slots from the circular file in chronological order
// (oldest of the requested window first). Default 20.
static int cmd_storage_log_tail(int argc, char **argv)
{
    int n = 20;
    if (argc >= 2) {
        n = atoi(argv[1]);
        if (n <= 0) {
            n = 20;
        }
    }
    if (!s_mounted) {
        printf("FAILED (not mounted)\n");
        return 1;
    }
    if (!ensure_log_file()) {
        printf("FAILED (log file unavailable)\n");
        return 1;
    }

    uint32_t total_written = s_file_hdr.wrap_count > 0 ? LOG_SLOT_COUNT : s_file_hdr.write_index;
    if ((uint32_t)n > total_written) {
        n = (int)total_written;
    }
    if (n == 0) {
        printf("(empty)\n");
        return 0;
    }

    char path[80];
    snprintf(path, sizeof(path), "%s%s", s_mount_point, LOG_FILE_NAME);
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("FAILED (could not open)\n");
        return 1;
    }

    uint32_t start = (s_file_hdr.write_index + LOG_SLOT_COUNT - (uint32_t)n) % LOG_SLOT_COUNT;
    char buf[LOG_SLOT_SIZE + 1];
    for (int i = 0; i < n; i++) {
        uint32_t idx = (start + (uint32_t)i) % LOG_SLOT_COUNT;
        long offset = (long)LOG_HEADER_SIZE + (long)idx * LOG_SLOT_SIZE;
        fseek(f, offset, SEEK_SET);
        size_t got = fread(buf, 1, LOG_SLOT_SIZE, f);
        buf[got] = '\0';
        if (buf[0] != '\0') {
            printf("%s\n", buf);
        }
    }
    fclose(f);
    return 0;
}

static int cmd_storage_log_level(int argc, char **argv)
{
    esp_log_level_t current = s_cfg.log_sink_min_level ? s_cfg.log_sink_min_level : ESP_LOG_WARN;
    if (argc < 2) {
        printf("current min_level=%d (1=ERROR 2=WARN 3=INFO 4=DEBUG)\n", (int)current);
        return 0;
    }
    int lvl = atoi(argv[1]);
    if (lvl < ESP_LOG_ERROR || lvl > ESP_LOG_DEBUG) {
        printf("usage: storage log level [1-4]  (1=ERROR 2=WARN 3=INFO 4=DEBUG)\n");
        return 1;
    }
    s_cfg.log_sink_min_level = (esp_log_level_t)lvl;
    if (s_log_sink_active) {
        s_log->unregister_sink(storage_log_sink);
        s_log->register_sink(storage_log_sink, NULL, (esp_log_level_t)lvl);
    }
    persist_log_level((esp_log_level_t)lvl);
    printf("OK (min_level=%d) — takes effect immediately and persists across reboot\n", lvl);
    return 0;
}

static int cmd_storage_log(int argc, char **argv)
{
    static const char *usage = "usage: storage log <status|tail|level> ...\n";
    if (argc < 2) {
        printf("%s", usage);
        return 1;
    }
    const char *sub = argv[1];
    if (strcmp(sub, "status") == 0) {
        return cmd_storage_log_status(argc - 1, argv + 1);
    } else if (strcmp(sub, "tail") == 0) {
        return cmd_storage_log_tail(argc - 1, argv + 1);
    } else if (strcmp(sub, "level") == 0) {
        return cmd_storage_log_level(argc - 1, argv + 1);
    }
    printf("unknown subcommand '%s'\n", sub);
    printf("%s", usage);
    return 1;
}

// Single top-level "storage" command with subcommands, dispatched on
// argv[1] — replaces the old flat storage_ls/storage_cat/etc. commands.
// Each handler above still receives argv shifted by one, so argv[1] in
// a handler's own frame is the path/first parameter, same as before.
static int cmd_storage(int argc, char **argv)
{
    static const char *usage =
        "usage: storage <info|ls|cd|cat|write|rm|mkdir|log> [path] [text...]\n";

    if (argc < 2) {
        printf("%s", usage);
        return 1;
    }

    const char *sub = argv[1];
    if (strcmp(sub, "info") == 0) {
        return cmd_storage_info(argc - 1, argv + 1);
    } else if (strcmp(sub, "ls") == 0) {
        return cmd_storage_ls(argc - 1, argv + 1);
    } else if (strcmp(sub, "cd") == 0) {
        return cmd_storage_cd(argc - 1, argv + 1);
    } else if (strcmp(sub, "cat") == 0) {
        return cmd_storage_cat(argc - 1, argv + 1);
    } else if (strcmp(sub, "write") == 0) {
        return cmd_storage_write(argc - 1, argv + 1);
    } else if (strcmp(sub, "rm") == 0) {
        return cmd_storage_rm(argc - 1, argv + 1);
    } else if (strcmp(sub, "mkdir") == 0) {
        return cmd_storage_mkdir(argc - 1, argv + 1);
    } else if (strcmp(sub, "log") == 0) {
        return cmd_storage_log(argc - 1, argv + 1);
    }

    printf("unknown subcommand '%s'\n", sub);
    printf("%s", usage);
    return 1;
}

static esp_err_t register_cli_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "storage",
        .help = "SD card file navigation/management + persistent log sink: "
                "info | ls [path] | cd [path] | cat <path> | "
                "write <path> <text...> | rm <path> | mkdir <path> | "
                "log <status|tail|level> ...",
        .hint = "<info|ls|cd|cat|write|rm|mkdir|log> [path] [text...]",
        .func = &cmd_storage,
    };

    esp_err_t err = esp_console_cmd_register(&cmd);
    if (err != ESP_OK) {
        s_log->error(TAG, "init: failed to register 'storage' command: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

/* ---------- mount helpers ---------- */

static esp_err_t mount_sdmmc(bool four_bit)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = s_cfg.format_if_mount_failed,
        .max_files              = s_cfg.max_open_files ? s_cfg.max_open_files : STORAGE_DEFAULT_MAX_FILES,
        .allocation_unit_size   = 0,   // let the driver pick based on card size
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = four_bit ? 4 : 1;

    // ESP32-S3: SDMMC pins are GPIO-matrix-routed, not fixed IOMUX — the
    // slot defaults above don't point at real pins on this chip, so they
    // must always be set explicitly from config.
    slot_config.clk = (gpio_num_t)s_cfg.sdmmc_clk_gpio;
    slot_config.cmd = (gpio_num_t)s_cfg.sdmmc_cmd_gpio;
    slot_config.d0  = (gpio_num_t)s_cfg.sdmmc_d0_gpio;
    if (four_bit) {
        slot_config.d1 = (gpio_num_t)s_cfg.sdmmc_d1_gpio;
        slot_config.d2 = (gpio_num_t)s_cfg.sdmmc_d2_gpio;
        slot_config.d3 = (gpio_num_t)s_cfg.sdmmc_d3_gpio;
    }

    return esp_vfs_fat_sdmmc_mount(s_mount_point, &host, &slot_config,
                                    &mount_config, &s_card);
}

static esp_err_t mount_spi(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = s_cfg.format_if_mount_failed,
        .max_files              = s_cfg.max_open_files ? s_cfg.max_open_files : STORAGE_DEFAULT_MAX_FILES,
        .allocation_unit_size   = 0,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = s_cfg.spi_mosi_gpio,
        .miso_io_num     = s_cfg.spi_miso_gpio,
        .sclk_io_num     = s_cfg.spi_sclk_gpio,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t err = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        return err;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs  = s_cfg.spi_cs_gpio;
    slot_config.host_id  = host.slot;

    return esp_vfs_fat_sdspi_mount(s_mount_point, &host, &slot_config,
                                    &mount_config, &s_card);
}

/* ---------- lifecycle ---------- */

static esp_err_t mod_init(module_t *self, void *config)
{
    (void)self;

    module_t *log_mod = module_registry_get("serial_log");
    if (!log_mod || !log_mod->iface) {
        return ESP_ERR_INVALID_STATE;
    }
    s_log = (const serial_log_iface_t *)log_mod->iface;

    module_t *cli_mod = module_registry_get("serial_cli");
    if (!cli_mod) {
        s_log->error(TAG, "init: serial_cli dependency not registered");
        return ESP_ERR_INVALID_STATE;
    }

    if (!config) {
        s_log->error(TAG, "init: storage_config_t is required "
                          "(SDMMC pins have no safe default on ESP32-S3)");
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *(storage_config_t *)config;

    if (s_cfg.bus_mode == STORAGE_BUS_SPI) {
        if (s_cfg.spi_cs_gpio < 0 || s_cfg.spi_mosi_gpio < 0 ||
            s_cfg.spi_miso_gpio < 0 || s_cfg.spi_sclk_gpio < 0) {
            s_log->error(TAG, "init: SPI bus_mode requires all spi_*_gpio fields set");
            return ESP_ERR_INVALID_ARG;
        }
    } else {
        // STORAGE_BUS_SDMMC_1BIT / _4BIT
        if (s_cfg.sdmmc_clk_gpio < 0 || s_cfg.sdmmc_cmd_gpio < 0 || s_cfg.sdmmc_d0_gpio < 0) {
            s_log->error(TAG, "init: SDMMC bus_mode requires clk/cmd/d0 gpio fields set "
                              "(ESP32-S3 has no usable SDMMC pin default)");
            return ESP_ERR_INVALID_ARG;
        }
        if (s_cfg.bus_mode == STORAGE_BUS_SDMMC_4BIT &&
            (s_cfg.sdmmc_d1_gpio < 0 || s_cfg.sdmmc_d2_gpio < 0 || s_cfg.sdmmc_d3_gpio < 0)) {
            s_log->error(TAG, "init: SDMMC 4-bit mode requires d1/d2/d3 gpio fields set");
            return ESP_ERR_INVALID_ARG;
        }
    }

    const char *mp = s_cfg.mount_point ? s_cfg.mount_point : STORAGE_DEFAULT_MOUNT_POINT;
    strncpy(s_mount_point, mp, sizeof(s_mount_point) - 1);
    s_mount_point[sizeof(s_mount_point) - 1] = '\0';

    // CLI cwd starts at the mount root regardless of whether the mount
    // itself succeeds below — `storage cd`/`ls`/etc. just fail cleanly
    // against an unmounted card rather than needing special-casing here.
    strncpy(s_cwd, s_mount_point, sizeof(s_cwd) - 1);
    s_cwd[sizeof(s_cwd) - 1] = '\0';

    esp_err_t mount_err;
    switch (s_cfg.bus_mode) {
        case STORAGE_BUS_SDMMC_4BIT:
            mount_err = mount_sdmmc(true);
            break;
        case STORAGE_BUS_SPI:
            mount_err = mount_spi();
            break;
        case STORAGE_BUS_SDMMC_1BIT:
        default:
            mount_err = mount_sdmmc(false);
            break;
    }

    if (mount_err != ESP_OK) {
        s_log->error(TAG, "init: mount failed at '%s': %s",
                     s_mount_point, esp_err_to_name(mount_err));
        s_mounted = false;
        // Not returning an error here — see Gotchas in storage.md.
        // Flip this to `return mount_err;` if a missing card should fail boot.
    } else {
        s_mounted = true;
        s_log->info(TAG, "init: mounted at '%s'", s_mount_point);
    }

    esp_err_t cli_err = register_cli_commands();
    if (cli_err != ESP_OK) {
        return cli_err;
    }

    // --- Persistent log sink setup ---
    if (!s_pending_mutex) {
        s_pending_mutex = xSemaphoreCreateMutex();
    }
    if (!s_flusher_exited) {
        s_flusher_exited = xSemaphoreCreateBinary();
    }
    if (!s_pending_mutex || !s_flusher_exited) {
        s_log->error(TAG, "init: failed to create log-sink sync primitives");
        return ESP_ERR_NO_MEM;
    }

    if (s_mounted) {
        recover_previous_boot_log();
    }

    if (!s_cfg.log_sink_disable) {
        esp_log_level_t default_level = s_cfg.log_sink_min_level ? s_cfg.log_sink_min_level : ESP_LOG_WARN;
        esp_log_level_t min_level = load_persisted_log_level(default_level);
        if (min_level != default_level) {
            s_log->info(TAG, "log: using persisted min_level=%d (config default was %d)",
                        (int)min_level, (int)default_level);
        }
        s_cfg.log_sink_min_level = min_level;   // keep `storage log status`/`level` in sync
        esp_err_t sink_err = s_log->register_sink(storage_log_sink, NULL, min_level);
        if (sink_err != ESP_OK) {
            s_log->error(TAG, "init: failed to register as a serial_log sink: %s",
                         esp_err_to_name(sink_err));
            // Not fatal — storage still works as a filesystem either way,
            // it just won't durably log. Same degrade-don't-block stance
            // as an unmounted card.
        } else {
            s_log_sink_active = true;
        }
    }

    return ESP_OK;
}

static esp_err_t mod_start(module_t *self)
{
    (void)self;
    if (s_log_sink_active && !s_flusher_task) {
        s_flusher_run = true;
        // 4096, matching espnow's recv task — this one additionally walks
        // the fopen/fwrite/fseek/fsync -> esp_vfs -> FatFs -> sdmmc driver
        // call chain, which is deeper than espnow's "copy into a queue".
        BaseType_t ok = xTaskCreate(log_flusher_task, "storage_logflush", 4096, NULL,
                                     tskIDLE_PRIORITY + 2, &s_flusher_task);
        if (ok != pdPASS) {
            s_log->error(TAG, "start: failed to create log flusher task");
            s_flusher_run = false;
            s_flusher_task = NULL;
        }
    }
    return ESP_OK;
}

static esp_err_t mod_stop(module_t *self)
{
    (void)self;
    stop_log_flusher();
    return ESP_OK;
}

static esp_err_t mod_deinit(module_t *self)
{
    (void)self;

    // Unregister BEFORE unmounting/freeing anything else — serial_log
    // holds a raw function pointer into this module, and it's called from
    // arbitrary tasks. Leaving it registered past this point means the
    // very next log line anywhere in the system calls into a module that's
    // mid-teardown.
    if (s_log_sink_active) {
        s_log->unregister_sink(storage_log_sink);
        s_log_sink_active = false;
    }
    // Already called from mod_stop(), which the registry guarantees runs
    // first — called again here (idempotent, no-ops if already stopped)
    // only in case deinit is ever reached without a preceding stop.
    stop_log_flusher();

    if (s_mounted) {
        esp_vfs_fat_sdcard_unmount(s_mount_point, s_card);
        s_mounted = false;
        s_card = NULL;
    }
    s_log_file_ready = false;   // force header re-read/recreate on next mount
    return ESP_OK;
}

// Cheap, non-blocking, cached-state only. Unmounted is the meaningful
// signal here — a supervisor watching this can tell "card is gone" apart
// from "card is fine, just quiet" without probing the filesystem itself.
static esp_err_t mod_health_check(module_t *self)
{
    (void)self;
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static const char *s_deps[] = { "serial_log", "serial_cli" };

module_t storage_module = {
    .name      = "storage",
    .deps      = s_deps,
    .dep_count = 2,
    // Essential: not worth halting boot over a missing SD card, but it
    // backs the persistent log sink, so a failure here means losing the
    // record of every other failure. Escalates loudly and stays present in
    // safe mode. See docs/supervisor.md.
    .criticality = MODULE_ESSENTIAL,
    .init      = mod_init,
    .start     = mod_start,
    .stop      = mod_stop,
    .deinit    = mod_deinit,
    .health_check = mod_health_check,
    .state     = MODULE_STATE_UNINIT,
    .ctx       = NULL,
    .iface     = (void *)&s_iface,
};

// Self-registration — see module_registry_add()'s doc comment.
__attribute__((constructor))
static void register_self(void)
{
    module_registry_add(&storage_module);
}
