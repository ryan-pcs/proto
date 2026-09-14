#include "laser_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

// ── Log level cascade ─────────────────────────────────────────────────────────
// LASER_LOG_MAX implies LASER_LOG_NORMAL.
// Errors (ESP_LOGE) are always printed regardless of level.
#ifdef LASER_LOG_MAX
#define LASER_LOG_NORMAL
#endif

// TAG is always needed since ESP_LOGE is always active
static const char *TAG = "LASER_SENSOR";

// ── Shared results ────────────────────────────────────────────────────────────
volatile uint16_t g_laser_distance_mm[LASER_NUM_SENSORS] = {0};
volatile bool     g_laser_timeout[LASER_NUM_SENSORS]     = {false};

// ── Bus and device handles (new API) ──────────────────────────────────────────
// The new API separates the bus from devices attached to it.
// Bus handle  = the I2C controller itself (one per port)
// Dev handle  = a specific device on that bus (one per address)
static i2c_master_bus_handle_t s_bus_a       = NULL;
static i2c_master_bus_handle_t s_bus_b       = NULL;
static i2c_master_dev_handle_t s_laser_dev_a = NULL; // sensor(s) on bus A
static i2c_master_dev_handle_t s_laser_dev_b = NULL; // sensor(s) on bus B (via MUX)
static i2c_master_dev_handle_t s_mux_dev_a   = NULL; // MUX on bus A
static i2c_master_dev_handle_t s_mux_dev_b   = NULL; // MUX on bus B

// ── Task/timer state ──────────────────────────────────────────────────────────
static TaskHandle_t      s_poll_task  = NULL;
static TaskHandle_t      s_timer_task = NULL;
static TaskHandle_t      s_par_task_a = NULL;
static TaskHandle_t      s_par_task_b = NULL;
static SemaphoreHandle_t s_timer_sem  = NULL;
static SemaphoreHandle_t s_par_sem_a  = NULL;
static SemaphoreHandle_t s_par_sem_b  = NULL;
static gptimer_handle_t  s_timer_gpt  = NULL;
static gptimer_handle_t  s_par_gpt    = NULL;

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

// Return the correct laser device handle for a given sensor number.
static i2c_master_dev_handle_t dev_for_sensor(uint8_t sensor_num)
{
    return (sensor_num < 2) ? s_laser_dev_a : s_laser_dev_b;
}

// Return the correct MUX device handle for a given sensor number.
// static i2c_master_dev_handle_t mux_for_sensor(uint8_t sensor_num)
// {
//     return (sensor_num < 2) ? s_mux_dev_a : s_mux_dev_b;
// }

// Within each bus sensors are 0-indexed: sensor 0,1 → local 0,1; sensor 2,3 → local 0,1
static uint8_t local_index(uint8_t sensor_num)
{
    return sensor_num % 2;
}

/**
 * Select a MUX channel. Channel 0-1 maps to MUX channels 4-5.
 * If no MUX is present the NACK is silently ignored.
 */
// static void mux_select(i2c_master_dev_handle_t mux_dev, uint8_t local_idx)
// {
//     if (mux_dev == NULL) return;
//     uint8_t ch = (uint8_t)(local_idx + 4);
//     // Ignore error — if no MUX is wired this just NACKs harmlessly
//     i2c_master_transmit(mux_dev, &ch, 1, 10);
//     esp_rom_delay_us(300);
// }

/**
 * Write one byte to a register on the laser sensor.
 */
static esp_err_t laser_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t data)
{
    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "  write reg 0x%02X = 0x%02X", reg, data);
    #endif

    uint8_t buf[2] = {reg, data};
    return i2c_master_transmit(dev, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

/**
 * Read one byte from a register on the laser sensor.
 *
 * This sensor requires a FULL STOP between the write (register select) and
 * the read — it does NOT support repeated-start. There must also be a 1ms
 * gap between the two transactions (from the Arduino library).
 *
 * So we use two separate i2c_master_transmit / i2c_master_receive calls
 * instead of i2c_master_transmit_receive().
 */
static esp_err_t laser_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *out)
{
    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "  read  reg 0x%02X ...", reg);
    #endif

    // 1. Write register address + STOP
    esp_err_t ret = i2c_master_transmit(dev, &reg, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) return ret;

    // 2. 1ms gap — required by this sensor (from the original Arduino library)
    vTaskDelay(pdMS_TO_TICKS(1));

    // 3. Read 1 byte in a new transaction
    ret = i2c_master_receive(dev, out, 1, pdMS_TO_TICKS(100));

    #ifdef LASER_LOG_MAX
    if (ret == ESP_OK) ESP_LOGI(TAG, "  read  reg 0x%02X = 0x%02X", reg, *out);
    #endif

    return ret;
}

/**
 * Create a bus and attach both the sensor device and the MUX device to it.
 */
static esp_err_t init_bus(i2c_port_num_t port, int sda, int scl,
                           i2c_master_bus_handle_t *bus_out,
                           i2c_master_dev_handle_t *laser_out,
                           i2c_master_dev_handle_t *mux_out)
{
    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "init_bus: port=%d sda=%d scl=%d", port, sda, scl);
    #endif

    // 1. Create the bus
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port      = port,
        .sda_io_num    = sda,
        .scl_io_num    = scl,
        .clk_source    = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, bus_out);
    if (ret != ESP_OK) return ret;

    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "init_bus: bus created, adding laser device at 0x%02X", LASER_ADDR);
    #endif

    // 2. Add the laser sensor device
    i2c_device_config_t laser_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = LASER_ADDR,
        .scl_speed_hz    = LASER_I2C_FREQ,
    };
    ret = i2c_master_bus_add_device(*bus_out, &laser_cfg, laser_out);
    if (ret != ESP_OK) return ret;

    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "init_bus: laser device added, probing MUX at 0x%02X", MUX_ADDR);
    #endif

    // 3. Add the MUX device (will NACK if not present — that's fine)
    i2c_device_config_t mux_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = MUX_ADDR,
        .scl_speed_hz    = LASER_I2C_FREQ,
    };
    ret = i2c_master_bus_add_device(*bus_out, &mux_cfg, mux_out);
    if (ret != ESP_OK) {
        // Non-fatal — no MUX is a valid single-sensor config
        #ifdef LASER_LOG_NORMAL
        ESP_LOGW(TAG, "MUX not found on port %d (single sensor mode)", port);
        #endif
        *mux_out = NULL;
        ret = ESP_OK;
    }
    #ifdef LASER_LOG_MAX
    else {
        ESP_LOGI(TAG, "init_bus: MUX found and added");
    }
    #endif

    return ret;
}

static esp_err_t start_gptimer(gptimer_handle_t *handle,
                                gptimer_alarm_cb_t cb,
                                void *cb_arg)
{
    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "start_gptimer: creating timer at %dHz", LASER_SAMPLE_HZ);
    #endif

    gptimer_config_t cfg = {
        .clk_src       = GPTIMER_CLK_SRC_DEFAULT,
        .direction     = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&cfg, handle));

    gptimer_alarm_config_t alarm = {
        .alarm_count                = 1000000 / LASER_SAMPLE_HZ,
        .reload_count               = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(*handle, &alarm));

    gptimer_event_callbacks_t cbs = { .on_alarm = cb };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(*handle, &cbs, cb_arg));
    ESP_ERROR_CHECK(gptimer_enable(*handle));
    ESP_ERROR_CHECK(gptimer_start(*handle));

    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "start_gptimer: timer running");
    #endif

    return ESP_OK;
}

static void stop_gptimer(gptimer_handle_t *handle)
{
    if (*handle) {
        #ifdef LASER_LOG_MAX
        ESP_LOGI(TAG, "stop_gptimer: stopping and deleting timer");
        #endif
        gptimer_stop(*handle);
        gptimer_disable(*handle);
        gptimer_del_timer(*handle);
        *handle = NULL;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────────────────────

esp_err_t laser_i2c_init(void)
{
    #ifdef LASER_LOG_NORMAL
    ESP_LOGI(TAG, "Initialising I2C bus A (port %d, sda=%d, scl=%d)",
             LASER_I2C_PORT_A, LASER_SDA_PIN_A, LASER_SCL_PIN_A);
    #endif
    return init_bus(LASER_I2C_PORT_A, LASER_SDA_PIN_A, LASER_SCL_PIN_A,
                    &s_bus_a, &s_laser_dev_a, &s_mux_dev_a);
}

esp_err_t laser_i2c_init_parallel(void)
{
    #ifdef LASER_LOG_NORMAL
    ESP_LOGI(TAG, "Initialising I2C bus B (port %d, sda=%d, scl=%d)",
             LASER_I2C_PORT_B, LASER_SDA_PIN_B, LASER_SCL_PIN_B);
    #endif
    return init_bus(LASER_I2C_PORT_B, LASER_SDA_PIN_B, LASER_SCL_PIN_B,
                    &s_bus_b, &s_laser_dev_b, &s_mux_dev_b);
}

esp_err_t laser_sensor_init(uint8_t sensor_num)
{
    #ifdef LASER_LOG_NORMAL
    ESP_LOGI(TAG, "Sensor %u: initialising...", sensor_num);
    #endif

    i2c_master_dev_handle_t dev = dev_for_sensor(sensor_num);

    // Check device ID
    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "Sensor %u: reading device ID...", sensor_num);
    #endif
    uint8_t id = 0;
    esp_err_t ret = laser_read_reg(dev, REG_DEVICE_ID, &id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Sensor %u: I2C error reading device ID (0x%02X)", sensor_num, ret);
        return ret;
    }
    if (id != 0x47) {
        ESP_LOGE(TAG, "Sensor %u: bad device ID 0x%02X (expected 0x47)", sensor_num, id);
        return ESP_ERR_NOT_FOUND;
    }

    // Reset
    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "Sensor %u: resetting (writing 0x04 to REG_CONFIG)...", sensor_num);
    #endif
    ret = laser_write_reg(dev, REG_CONFIG, 0x04);
    if (ret != ESP_OK) return ret;

    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "Sensor %u: waiting 500ms for reset to complete...", sensor_num);
    #endif
    vTaskDelay(pdMS_TO_TICKS(500));

    // Enable
    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "Sensor %u: enabling (writing 0x02 to REG_CONFIG)...", sensor_num);
    #endif
    ret = laser_write_reg(dev, REG_CONFIG, 0x02);
    if (ret != ESP_OK) return ret;

    #ifdef LASER_LOG_NORMAL
    ESP_LOGI(TAG, "Sensor %u: OK", sensor_num);
    #endif
    return ESP_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// BASE — laser_get_distance
// ─────────────────────────────────────────────────────────────────────────────

esp_err_t laser_get_distance(uint8_t sensor_num, uint16_t *distance_mm)
{
    i2c_master_dev_handle_t dev = dev_for_sensor(sensor_num);

    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "Sensor %u: checking timeout bit...", sensor_num);
    #endif

    // Check timeout bit
    uint8_t cfg = 0;
    esp_err_t ret = laser_read_reg(dev, REG_CONFIG, &cfg);
    if (ret != ESP_OK) return ret;

    if (cfg & 0x01) {
        #ifdef LASER_LOG_MAX
        ESP_LOGI(TAG, "Sensor %u: timeout bit set, returning LASER_TIMEOUT_VAL", sensor_num);
        #endif
        *distance_mm = LASER_TIMEOUT_VAL;
        return ESP_ERR_TIMEOUT;
    }

    // Read distance
    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "Sensor %u: reading distance registers H and L...", sensor_num);
    #endif
    uint8_t high = 0, low = 0;
    ret = laser_read_reg(dev, REG_DISTANCE_H, &high);
    if (ret != ESP_OK) return ret;
    ret = laser_read_reg(dev, REG_DISTANCE_L, &low);
    if (ret != ESP_OK) return ret;

    *distance_mm = (uint16_t)((high << 8) | low);

    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "Sensor %u: distance = %u mm (raw H=0x%02X L=0x%02X)",
             sensor_num, *distance_mm, high, low);
    #endif

    return ESP_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// EXTENSION 1 — laser_get_distance_all
// ─────────────────────────────────────────────────────────────────────────────

esp_err_t laser_get_distance_all(uint16_t distances_mm[], uint8_t num_sensors)
{
    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "laser_get_distance_all: reading %u sensor(s) sequentially...", num_sensors);
    #endif

    esp_err_t first_err = ESP_OK;
    for (uint8_t i = 0; i < num_sensors; i++) {
        esp_err_t ret = laser_get_distance(i, &distances_mm[i]);
        if (ret != ESP_OK && first_err == ESP_OK) first_err = ret;
    }

    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "laser_get_distance_all: done");
    #endif

    return first_err;
}

// ─────────────────────────────────────────────────────────────────────────────
// EXTENSION 2 — laser_get_distance_polling
// ─────────────────────────────────────────────────────────────────────────────

static void polling_task(void *arg)
{
    uint8_t num = (uint8_t)(uintptr_t)arg;
    const TickType_t period = pdMS_TO_TICKS(1000 / LASER_SAMPLE_HZ);

    #ifdef LASER_LOG_NORMAL
    ESP_LOGI(TAG, "Polling task started! (%u sensor(s) at %dHz)", num, LASER_SAMPLE_HZ);
    #endif

    while (1) {
        #ifdef LASER_LOG_MAX
        ESP_LOGI(TAG, "Polling task: starting read cycle...");
        #endif

        for (uint8_t i = 0; i < num; i++) {
            uint16_t dist = 0;
            esp_err_t ret = laser_get_distance(i, &dist);
            g_laser_distance_mm[i] = dist;
            g_laser_timeout[i]     = (ret == ESP_ERR_TIMEOUT);

            #ifdef LASER_LOG_NORMAL
            ESP_LOGI(TAG, "Sensor %u: %u mm%s", i, dist, g_laser_timeout[i] ? " (timeout)" : "");
            #endif
        }

        #ifdef LASER_LOG_MAX
        ESP_LOGI(TAG, "Polling task: cycle done, sleeping %dms", 1000 / LASER_SAMPLE_HZ);
        #endif

        vTaskDelay(period);
    }
}

esp_err_t laser_get_distance_polling(uint8_t num_sensors)
{
    if (s_poll_task) {
        #ifdef LASER_LOG_NORMAL
        ESP_LOGW(TAG, "Polling already running");
        #endif
        return ESP_ERR_INVALID_STATE;
    }

    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "laser_get_distance_polling: creating task for %u sensor(s)", num_sensors);
    #endif

    BaseType_t ok = xTaskCreate(polling_task, "laser_poll", 3072,
                                (void *)(uintptr_t)num_sensors, 5, &s_poll_task);
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

void laser_stop_polling(void)
{
    #ifdef LASER_LOG_NORMAL
    ESP_LOGI(TAG, "Stopping polling task");
    #endif
    if (s_poll_task) { vTaskDelete(s_poll_task); s_poll_task = NULL; }
}

// ─────────────────────────────────────────────────────────────────────────────
// EXTENSION 3 — laser_get_distance_timer
// ─────────────────────────────────────────────────────────────────────────────

static bool IRAM_ATTR timer_isr_cb(gptimer_handle_t timer,
                                    const gptimer_alarm_event_data_t *edata,
                                    void *arg)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_timer_sem, &woken);
    return (woken == pdTRUE);
}

static void timer_task(void *arg)
{
    uint8_t num = (uint8_t)(uintptr_t)arg;

    #ifdef LASER_LOG_NORMAL
    ESP_LOGI(TAG, "Timer task started! (%u sensor(s) at %dHz)", num, LASER_SAMPLE_HZ);
    #endif

    while (1) {
        if (xSemaphoreTake(s_timer_sem, portMAX_DELAY) == pdTRUE) {
            #ifdef LASER_LOG_MAX
            ESP_LOGI(TAG, "Timer task: semaphore taken, starting read cycle...");
            #endif

            for (uint8_t i = 0; i < num; i++) {
                uint16_t dist = 0;
                esp_err_t ret = laser_get_distance(i, &dist);
                g_laser_distance_mm[i] = dist;
                g_laser_timeout[i]     = (ret == ESP_ERR_TIMEOUT);

                #ifdef LASER_LOG_NORMAL
                ESP_LOGI(TAG, "Sensor %u: %u mm%s", i, dist, g_laser_timeout[i] ? " (timeout)" : "");
                #endif
            }

            #ifdef LASER_LOG_MAX
            ESP_LOGI(TAG, "Timer task: cycle done, waiting for next tick");
            #endif
        }
    }
}

esp_err_t laser_get_distance_timer(uint8_t num_sensors)
{
    if (s_timer_gpt) {
        #ifdef LASER_LOG_NORMAL
        ESP_LOGW(TAG, "Timer already running");
        #endif
        return ESP_ERR_INVALID_STATE;
    }

    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "laser_get_distance_timer: creating semaphore and task for %u sensor(s)...", num_sensors);
    #endif

    s_timer_sem = xSemaphoreCreateBinary();
    if (!s_timer_sem) return ESP_ERR_NO_MEM;

    BaseType_t ok = xTaskCreate(timer_task, "laser_timer", 3072,
                                (void *)(uintptr_t)num_sensors, 6, &s_timer_task);
    if (ok != pdPASS) {
        vSemaphoreDelete(s_timer_sem); s_timer_sem = NULL;
        return ESP_ERR_NO_MEM;
    }
    return start_gptimer(&s_timer_gpt, timer_isr_cb, NULL);
}

void laser_stop_timer(void)
{
    #ifdef LASER_LOG_NORMAL
    ESP_LOGI(TAG, "Stopping timer task");
    #endif
    stop_gptimer(&s_timer_gpt);
    if (s_timer_task) { vTaskDelete(s_timer_task); s_timer_task = NULL; }
    if (s_timer_sem)  { vSemaphoreDelete(s_timer_sem); s_timer_sem = NULL; }
}

// ─────────────────────────────────────────────────────────────────────────────
// EXTENSION 4 — laser_get_distance_parallel
// ─────────────────────────────────────────────────────────────────────────────

static bool IRAM_ATTR par_isr_cb(gptimer_handle_t timer,
                                   const gptimer_alarm_event_data_t *edata,
                                   void *arg)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_par_sem_a, &woken);
    xSemaphoreGiveFromISR(s_par_sem_b, &woken);
    return (woken == pdTRUE);
}

static void par_task_a(void *arg)
{
    uint8_t num = (uint8_t)(uintptr_t)arg;

    #ifdef LASER_LOG_NORMAL
    ESP_LOGI(TAG, "Parallel task A started! (core 0, bus A, %u sensor(s))", num);
    #endif

    while (1) {
        if (xSemaphoreTake(s_par_sem_a, portMAX_DELAY) == pdTRUE) {
            #ifdef LASER_LOG_MAX
            ESP_LOGI(TAG, "Par task A: semaphore taken, reading bus A sensors...");
            #endif

            for (uint8_t i = 0; i < num; i++) {
                uint16_t dist = 0;
                esp_err_t ret = laser_get_distance(i, &dist);
                g_laser_distance_mm[i] = dist;
                g_laser_timeout[i]     = (ret == ESP_ERR_TIMEOUT);

                #ifdef LASER_LOG_NORMAL
                ESP_LOGI(TAG, "Sensor %u: %u mm%s", i, dist, g_laser_timeout[i] ? " (timeout)" : "");
                #endif
            }

            #ifdef LASER_LOG_MAX
            ESP_LOGI(TAG, "Par task A: cycle done");
            #endif
        }
    }
}

static void par_task_b(void *arg)
{
    uint8_t num = (uint8_t)(uintptr_t)arg;

    #ifdef LASER_LOG_NORMAL
    ESP_LOGI(TAG, "Parallel task B started! (core 1, bus B, %u sensor(s))", num);
    #endif

    while (1) {
        if (xSemaphoreTake(s_par_sem_b, portMAX_DELAY) == pdTRUE) {
            #ifdef LASER_LOG_MAX
            ESP_LOGI(TAG, "Par task B: semaphore taken, reading bus B sensors...");
            #endif

            for (uint8_t i = 2; i < 2 + num; i++) {
                uint16_t dist = 0;
                esp_err_t ret = laser_get_distance(i, &dist);
                g_laser_distance_mm[i] = dist;
                g_laser_timeout[i]     = (ret == ESP_ERR_TIMEOUT);

                #ifdef LASER_LOG_NORMAL
                ESP_LOGI(TAG, "Sensor %u: %u mm%s", i, dist, g_laser_timeout[i] ? " (timeout)" : "");
                #endif
            }

            #ifdef LASER_LOG_MAX
            ESP_LOGI(TAG, "Par task B: cycle done");
            #endif
        }
    }
}

esp_err_t laser_get_distance_parallel(uint8_t num_sensors)
{
    if (s_par_gpt) {
        #ifdef LASER_LOG_NORMAL
        ESP_LOGW(TAG, "Parallel already running");
        #endif
        return ESP_ERR_INVALID_STATE;
    }
    if (num_sensors != 2 && num_sensors != 4) {
        ESP_LOGE(TAG, "Parallel requires 2 or 4 sensors, got %u", num_sensors);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t per_bus = num_sensors / 2;

    #ifdef LASER_LOG_MAX
    ESP_LOGI(TAG, "laser_get_distance_parallel: creating semaphores and pinning tasks...");
    #endif

    s_par_sem_a = xSemaphoreCreateBinary();
    s_par_sem_b = xSemaphoreCreateBinary();
    if (!s_par_sem_a || !s_par_sem_b) return ESP_ERR_NO_MEM;

    BaseType_t ok_a = xTaskCreatePinnedToCore(par_task_a, "laser_par_a", 3072,
                                              (void *)(uintptr_t)per_bus,
                                                6, &s_par_task_a, 0);
    BaseType_t ok_b = xTaskCreatePinnedToCore(par_task_b, "laser_par_b", 3072,
                                              (void *)(uintptr_t)per_bus,
                                                6, &s_par_task_b, 1);

    if (ok_a != pdPASS || ok_b != pdPASS) {
        if (s_par_task_a) { vTaskDelete(s_par_task_a); s_par_task_a = NULL; }
        if (s_par_task_b) { vTaskDelete(s_par_task_b); s_par_task_b = NULL; }
        vSemaphoreDelete(s_par_sem_a); s_par_sem_a = NULL;
        vSemaphoreDelete(s_par_sem_b); s_par_sem_b = NULL;
        return ESP_ERR_NO_MEM;
    }

    #ifdef LASER_LOG_NORMAL
    ESP_LOGI(TAG, "Parallel: task A → core 0 (bus A), task B → core 1 (bus B)");
    #endif

    return start_gptimer(&s_par_gpt, par_isr_cb, NULL);
}

void laser_stop_parallel(void)
{
    #ifdef LASER_LOG_NORMAL
    ESP_LOGI(TAG, "Stopping parallel tasks");
    #endif
    stop_gptimer(&s_par_gpt);
    if (s_par_task_a) { vTaskDelete(s_par_task_a); s_par_task_a = NULL; }
    if (s_par_task_b) { vTaskDelete(s_par_task_b); s_par_task_b = NULL; }
    if (s_par_sem_a)  { vSemaphoreDelete(s_par_sem_a); s_par_sem_a = NULL; }
    if (s_par_sem_b)  { vSemaphoreDelete(s_par_sem_b); s_par_sem_b = NULL; }
}