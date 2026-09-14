#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_log.h"
#include "module.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STORAGE_BUS_SDMMC_1BIT = 0,
    STORAGE_BUS_SDMMC_4BIT,
    STORAGE_BUS_SPI,
} storage_bus_mode_t;

typedef struct {
    storage_bus_mode_t bus_mode;         // default STORAGE_BUS_SDMMC_1BIT (0)
    const char        *mount_point;      // NULL -> "/sdcard"
    int                max_open_files;   // 0 -> default 5
    bool               format_if_mount_failed;

    // Only read when bus_mode == STORAGE_BUS_SDMMC_1BIT or _4BIT.
    // Required on ESP32-S3 — unlike the original ESP32, SDMMC signals are
    // routed through the GPIO matrix rather than fixed IOMUX pins, so
    // there's no usable "default" and these must match your board's
    // actual wiring.
    int sdmmc_clk_gpio;
    int sdmmc_cmd_gpio;
    int sdmmc_d0_gpio;
    int sdmmc_d1_gpio;   // only read in 4-bit mode
    int sdmmc_d2_gpio;   // only read in 4-bit mode
    int sdmmc_d3_gpio;   // only read in 4-bit mode

    // Only read when bus_mode == STORAGE_BUS_SPI
    int spi_cs_gpio;
    int spi_mosi_gpio;
    int spi_miso_gpio;
    int spi_sclk_gpio;

    // --- Persistent log sink (see docs/supervisor.md) ---
    //
    // When enabled (the default), storage registers itself with serial_log
    // as a sink and mirrors WARN+ lines to a preallocated, fixed-size
    // circular file on the card — durable across a reset in a way the
    // console alone isn't. A missing/unmounted card degrades this
    // silently (nothing queues forever, nothing blocks); it never affects
    // storage's normal filesystem functionality either way.
    bool            log_sink_disable;    // true -> never register as a sink
    esp_log_level_t log_sink_min_level;  // 0 -> default ESP_LOG_WARN
} storage_config_t;

/* Callback for list_dir(). Return false to stop iteration early. */
typedef bool (*storage_dirent_cb_t)(const char *name, bool is_dir,
                                     size_t size, void *user_ctx);

typedef struct {
    const char *(*get_mount_point)(void);
    bool        (*is_mounted)(void);
    esp_err_t   (*get_space)(uint64_t *total_bytes, uint64_t *free_bytes);
    esp_err_t   (*exists)(const char *path, bool *out_exists);
    esp_err_t   (*mkdir)(const char *path);
    esp_err_t   (*remove)(const char *path);
    esp_err_t   (*list_dir)(const char *path, storage_dirent_cb_t cb, void *user_ctx);
} storage_iface_t;

extern module_t storage_module;

#ifdef __cplusplus
}
#endif