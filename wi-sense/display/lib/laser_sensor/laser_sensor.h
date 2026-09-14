#ifndef LASER_SENSOR_H
#define LASER_SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"  // new ESP-IDF 5.x API
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── LOGGING ──────────────────────────────────────────────────────────────────
// Uncomment ONE level. If none are uncommented, only errors are printed.
//
// #define LASER_LOG_NORMAL  // Task start/stop, sensor OK, warnings
// #define LASER_LOG_MAX     // Everything above + every read/write/step

// ── I2C addresses ─────────────────────────────────────────────────────────────
#define LASER_ADDR              0x26    // Fixed sensor address (cannot change)
#define MUX_ADDR                0x70    // PCA954X multiplexer address

// ── Sensor registers ──────────────────────────────────────────────────────────
#define REG_DEVICE_ID           0x01    // Returns 0x47 when alive
#define REG_CONFIG              0x02    // 0x04=reset, 0x02=enable; bit0=timeout
#define REG_DISTANCE_H          0x03    // Distance high byte
#define REG_DISTANCE_L          0x04    // Distance low byte

// ── I2C bus A (sensors 0-1) ───────────────────────────────────────────────────
#define LASER_I2C_PORT_A        I2C_NUM_0
#define LASER_SDA_PIN_A         21   // was 1 - GPIO 1 is the USB serial line
#define LASER_SCL_PIN_A         22   // was 2 - GPIO 2 is a boot-mode pin

// ── I2C bus B (sensors 2-3, parallel version only) ────────────────────────────
#define LASER_I2C_PORT_B        I2C_NUM_1
#define LASER_SDA_PIN_B         36      // unused here. Was 6 - that is a flash pin.
#define LASER_SCL_PIN_B         39      // unused here. Was 7 - that is a flash pin.

// ── General config ────────────────────────────────────────────────────────────
#define LASER_I2C_FREQ          100000  // 100 kHz — sensor is slow, keep it safe
#define LASER_NUM_SENSORS       4       // Max supported sensors
#define LASER_SAMPLE_HZ         50      // Sensor fixed sample rate (50Hz = 20ms)
#define LASER_TIMEOUT_VAL       8191    // Distance value returned on timeout

// ── Shared results ────────────────────────────────────────────────────────────
// All background variants write here. Read from anywhere in your code.
// Index = sensor_num passed to laser_sensor_init().
extern volatile uint16_t g_laser_distance_mm[LASER_NUM_SENSORS];
extern volatile bool     g_laser_timeout[LASER_NUM_SENSORS];

// ─────────────────────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Initialise I2C bus A (always required).
 * Call once from app_main before anything else.
 */
esp_err_t laser_i2c_init(void);

/**
 * @brief  Initialise I2C bus B (only needed for parallel version).
 */
esp_err_t laser_i2c_init_parallel(void);

/**
 * @brief  Initialise one sensor. Checks device ID, resets, enables.
 * sensor_num 0-1 → bus A MUX channels 4-5.
 * sensor_num 2-3 → bus B MUX channels 4-5 (parallel version only).
 * With a single sensor and no MUX, always pass 0.
 */
esp_err_t laser_sensor_init(uint8_t sensor_num);

// ─────────────────────────────────────────────────────────────────────────────
// BASE — laser_get_distance
//   Blocking read of one sensor. ~3-4ms per call.
//   The foundation all other variants are built on.
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Read distance from one sensor immediately (blocking).
 * @param  sensor_num    Sensor index (0-3).
 * @param  distance_mm   Output distance in mm.
 * @return ESP_OK, ESP_ERR_TIMEOUT, or an I2C error code.
 */
esp_err_t laser_get_distance(uint8_t sensor_num, uint16_t *distance_mm);

// ─────────────────────────────────────────────────────────────────────────────
// EXTENSION 1 — laser_get_distance_all
//   Calls laser_get_distance() for each sensor in sequence (blocking).
//   Total time ≈ num_sensors × 3.5ms.
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Read all sensors sequentially, blocking until all are done.
 * @param  distances_mm  Output array, must hold at least num_sensors elements.
 * @param  num_sensors   How many sensors to read (1-LASER_NUM_SENSORS).
 */
esp_err_t laser_get_distance_all(uint16_t distances_mm[], uint8_t num_sensors);

// ─────────────────────────────────────────────────────────────────────────────
// EXTENSION 2 — laser_get_distance_polling
//   FreeRTOS task reads all sensors then sleeps 20ms. Non-blocking after start.
//   Results → g_laser_distance_mm[].
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Start background polling task.
 * @param  num_sensors  Number of sensors to poll (1-LASER_NUM_SENSORS).
 */
esp_err_t laser_get_distance_polling(uint8_t num_sensors);

/** @brief  Stop the polling task. */
void laser_stop_polling(void);

// ─────────────────────────────────────────────────────────────────────────────
// EXTENSION 3 — laser_get_distance_timer
//   Hardware timer ISR fires every 20ms, wakes a task via semaphore.
//   Precise 50Hz schedule. Results → g_laser_distance_mm[].
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Start timer-driven background reads.
 * @param  num_sensors  Number of sensors to read (1-LASER_NUM_SENSORS).
 */
esp_err_t laser_get_distance_timer(uint8_t num_sensors);

/** @brief  Stop the timer and its task. */
void laser_stop_timer(void);

// ─────────────────────────────────────────────────────────────────────────────
// EXTENSION 4 — laser_get_distance_parallel
//   One timer ISR, two tasks pinned to separate cores, two I2C buses.
//   Task A → core 0 → bus A → sensors 0,1
//   Task B → core 1 → bus B → sensors 2,3
//   ~7ms read time (half of sequential). Precise 50Hz schedule.
//   Results → g_laser_distance_mm[].
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief  Start parallel dual-bus timer-driven reads.
 * Requires laser_i2c_init_parallel() called beforehand.
 * @param  num_sensors  Must be 2 or 4.
 */
esp_err_t laser_get_distance_parallel(uint8_t num_sensors);

/** @brief  Stop the parallel timer and both tasks. */
void laser_stop_parallel(void);

#ifdef __cplusplus
}
#endif

#endif // LASER_SENSOR_H