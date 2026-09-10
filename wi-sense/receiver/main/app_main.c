/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Get Start Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "nvs_flash.h"

#include "esp_mac.h"
#include "rom/ets_sys.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"

#define CONFIG_LESS_INTERFERENCE_CHANNEL   11
#define CSI_TX_SSID                         "CSI_TX"
#define CSI_TX_PASSWORD                     "csi12345"
#if CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C61 || (CONFIG_IDF_TARGET_ESP32C6 && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0))
#define CONFIG_WIFI_BAND_MODE               WIFI_BAND_MODE_2G_ONLY
#define CONFIG_WIFI_2G_BANDWIDTHS           WIFI_BW_HT40
#define CONFIG_WIFI_5G_BANDWIDTHS           WIFI_BW_HT40
#define CONFIG_WIFI_2G_PROTOCOL             WIFI_PROTOCOL_11N
#define CONFIG_WIFI_5G_PROTOCOL             WIFI_PROTOCOL_11N
#else
#define CONFIG_WIFI_BANDWIDTH           WIFI_BW_HT20
#endif

#define CONFIG_FORCE_GAIN                   0
#define CSI_QUEUE_LENGTH                    2
#define CSI_MAX_DATA_LENGTH                512

#if CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C61
#define CSI_FORCE_LLTF                      0
#endif

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32C61
#define CONFIG_GAIN_CONTROL                 1
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
#define ESP_IF_WIFI_STA ESP_MAC_WIFI_STA
#endif

static const char *TAG = "csi_recv";
static volatile bool wifi_connected = false;
static volatile bool csi_ready = false;
static esp_netif_t *wifi_netif = NULL;
static uint8_t transmitter_bssid[6] = {0};
static volatile bool transmitter_bssid_ready = false;
static volatile bool capture_enabled = false;
static volatile uint32_t capture_frame_count = 0;
static volatile uint32_t capture_queue_drops = 0;
static QueueHandle_t csi_queue = NULL;
static volatile bool csi_output_busy = false;
static volatile bool csi_callback_busy = false;
static portMUX_TYPE csi_state_mux = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    uint32_t id;
    uint8_t mac[6];
    uint16_t len;
    uint8_t first_word_invalid;
    int16_t data[CSI_MAX_DATA_LENGTH];
    wifi_pkt_rx_ctrl_t rx_ctrl;
    uint8_t agc_gain;
    int8_t fft_gain;
} csi_frame_t;

static void refresh_transmitter_bssid(void)
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        memcpy(transmitter_bssid, ap_info.bssid, sizeof(transmitter_bssid));
        transmitter_bssid_ready = true;
        ets_printf("CSI_SOURCE_BSSID=" MACSTR "\n", MAC2STR(transmitter_bssid));
    } else {
        transmitter_bssid_ready = false;
        ets_printf("CSI_SOURCE_BSSID_UNAVAILABLE\n");
    }
}

static void emit_csi_frame(const csi_frame_t *frame)
{
    if (frame->id == 0) {
#if CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32C61
        ets_printf("================ CSI RECV ================\n");
        ets_printf("type,id,mac,rssi,rate,noise_floor,fft_gain,agc_gain,channel,local_timestamp,sig_len,rx_format,len,first_word,data\n");
#else
        ets_printf("================ CSI RECV ================\n");
        ets_printf("type,id,mac,rssi,rate,sig_mode,mcs,bandwidth,smoothing,not_sounding,aggregation,stbc,fec_coding,sgi,noise_floor,ampdu_cnt,channel,secondary_channel,local_timestamp,ant,sig_len,sig_mode,len,first_word,data\n");
#endif
    }
#if CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32C61
    ets_printf("CSI_DATA,%lu," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d",
               (unsigned long)frame->id, MAC2STR(frame->mac), frame->rx_ctrl.rssi,
               frame->rx_ctrl.rate, frame->rx_ctrl.noise_floor, frame->fft_gain,
               frame->agc_gain, frame->rx_ctrl.channel, frame->rx_ctrl.timestamp,
               frame->rx_ctrl.sig_len, frame->rx_ctrl.cur_bb_format);
#else
    ets_printf("CSI_DATA,%lu," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
               (unsigned long)frame->id, MAC2STR(frame->mac), frame->rx_ctrl.rssi,
               frame->rx_ctrl.rate, frame->rx_ctrl.sig_mode, frame->rx_ctrl.mcs,
               frame->rx_ctrl.cwb, frame->rx_ctrl.smoothing, frame->rx_ctrl.not_sounding,
               frame->rx_ctrl.aggregation, frame->rx_ctrl.stbc, frame->rx_ctrl.fec_coding,
               frame->rx_ctrl.sgi, frame->rx_ctrl.noise_floor, frame->rx_ctrl.ampdu_cnt,
               frame->rx_ctrl.channel, frame->rx_ctrl.secondary_channel,
               frame->rx_ctrl.timestamp, frame->rx_ctrl.ant, frame->rx_ctrl.sig_len,
               frame->rx_ctrl.sig_mode);
#endif
    ets_printf(",%d,%d,\"[%d", frame->len, frame->first_word_invalid, frame->data[0]);
    for (int index = 1; index < frame->len; index++) {
        ets_printf(",%d", frame->data[index]);
    }
    ets_printf("]\"\n");
}

static void csi_output_task(void *argument)
{
    (void)argument;
    csi_frame_t frame;
    while (true) {
        portENTER_CRITICAL(&csi_state_mux);
        csi_output_busy = true;
        portEXIT_CRITICAL(&csi_state_mux);
        if (xQueueReceive(csi_queue, &frame, pdMS_TO_TICKS(10)) == pdTRUE) {
            emit_csi_frame(&frame);
        }
        portENTER_CRITICAL(&csi_state_mux);
        csi_output_busy = false;
        portEXIT_CRITICAL(&csi_state_mux);
    }
}

void receiver_capture_start(void)
{
    portENTER_CRITICAL(&csi_state_mux);
    capture_enabled = false;
    portEXIT_CRITICAL(&csi_state_mux);
    xQueueReset(csi_queue);
    capture_queue_drops = 0;
    capture_frame_count = 0;
    portENTER_CRITICAL(&csi_state_mux);
    capture_enabled = true;
    portEXIT_CRITICAL(&csi_state_mux);
}

bool receiver_capture_stop(void)
{
    portENTER_CRITICAL(&csi_state_mux);
    capture_enabled = false;
    portEXIT_CRITICAL(&csi_state_mux);
    for (int wait_ms = 0; wait_ms < 3000; wait_ms++) {
        if (!csi_callback_busy && !csi_output_busy && uxQueueMessagesWaiting(csi_queue) == 0) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return !csi_callback_busy && !csi_output_busy && uxQueueMessagesWaiting(csi_queue) == 0;
}

uint32_t receiver_capture_queue_drops(void)
{
    return capture_queue_drops;
}

bool receiver_is_connected(void)
{
    return wifi_connected;
}

bool receiver_is_ready(void)
{
    return csi_ready;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        transmitter_bssid_ready = false;
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_connected = true;
        ets_printf("WiFi connected to %s\n", CSI_TX_SSID);
        refresh_transmitter_bssid();
    }
}

static void wifi_init()
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());
    wifi_netif = esp_netif_create_default_wifi_sta();
    if (!wifi_netif) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CSI_TX_SSID,
            .password = CSI_TX_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 2);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(wifi_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(wifi_netif, &ip_info));

#if CONFIG_IDF_TARGET_ESP32C5
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_band_mode(CONFIG_WIFI_BAND_MODE);
    wifi_protocols_t protocols = {
        .ghz_2g = CONFIG_WIFI_2G_PROTOCOL,
        .ghz_5g = CONFIG_WIFI_5G_PROTOCOL
    };
    ESP_ERROR_CHECK(esp_wifi_set_protocols(ESP_IF_WIFI_STA, &protocols));
    wifi_bandwidths_t bandwidth = {
        .ghz_2g = CONFIG_WIFI_2G_BANDWIDTHS,
        .ghz_5g = CONFIG_WIFI_5G_BANDWIDTHS
    };
    ESP_ERROR_CHECK(esp_wifi_set_bandwidths(ESP_IF_WIFI_STA, &bandwidth));
#elif (CONFIG_IDF_TARGET_ESP32C6 && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)) || CONFIG_IDF_TARGET_ESP32C61
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_band_mode(CONFIG_WIFI_BAND_MODE);
    wifi_protocols_t protocols = {
        .ghz_2g = CONFIG_WIFI_2G_PROTOCOL,
    };
    ESP_ERROR_CHECK(esp_wifi_set_protocols(ESP_IF_WIFI_STA, &protocols));
    wifi_bandwidths_t bandwidth = {
        .ghz_2g = CONFIG_WIFI_2G_BANDWIDTHS,
    };
    ESP_ERROR_CHECK(esp_wifi_set_bandwidths(ESP_IF_WIFI_STA, &bandwidth));
#else
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, CONFIG_WIFI_BANDWIDTH));
#endif

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
#if CONFIG_IDF_TARGET_ESP32C5
    if ((CONFIG_WIFI_BAND_MODE == WIFI_BAND_MODE_2G_ONLY && CONFIG_WIFI_2G_BANDWIDTHS == WIFI_BW_HT20)
            || (CONFIG_WIFI_BAND_MODE == WIFI_BAND_MODE_5G_ONLY && CONFIG_WIFI_5G_BANDWIDTHS == WIFI_BW_HT20)) {
        ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL, WIFI_SECOND_CHAN_NONE));
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL, WIFI_SECOND_CHAN_BELOW));
    }
#elif (CONFIG_IDF_TARGET_ESP32C6 && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)) || CONFIG_IDF_TARGET_ESP32C61
    if (CONFIG_WIFI_BAND_MODE == WIFI_BAND_MODE_2G_ONLY && CONFIG_WIFI_2G_BANDWIDTHS == WIFI_BW_HT20) {
        ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL, WIFI_SECOND_CHAN_NONE));
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL, WIFI_SECOND_CHAN_BELOW));
    }
#else
    if (CONFIG_WIFI_BANDWIDTH == WIFI_BW_HT20) {
        ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL, WIFI_SECOND_CHAN_NONE));
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL, WIFI_SECOND_CHAN_BELOW));
    }
#endif

    ESP_ERROR_CHECK(esp_wifi_connect());
}

static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    static int rejected_count = 0;

    if (!info || !info->buf) {
        ets_printf("CSI_INVALID,count=%d\n", ++rejected_count);
        return;
    }
    if (info->len < 2) {
        ets_printf("CSI_INVALID,count=%d\n", ++rejected_count);
        return;
    }
    if (!transmitter_bssid_ready || memcmp(info->mac, transmitter_bssid, sizeof(transmitter_bssid)) != 0) {
        return;
    }
    portENTER_CRITICAL(&csi_state_mux);
    if (!capture_enabled) {
        portEXIT_CRITICAL(&csi_state_mux);
        return;
    }
    csi_callback_busy = true;
    portEXIT_CRITICAL(&csi_state_mux);

    const wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;
    float compensate_gain = 1.0f;
    csi_frame_t frame = {0};
    frame.id = capture_frame_count;
    memcpy(frame.mac, info->mac, sizeof(frame.mac));
    frame.len = info->len > CSI_MAX_DATA_LENGTH ? CSI_MAX_DATA_LENGTH : info->len;
    frame.first_word_invalid = info->first_word_invalid;
    frame.rx_ctrl = *rx_ctrl;
    static uint8_t agc_gain = 0;
    static int8_t fft_gain = 0;
#if CONFIG_GAIN_CONTROL
    static uint8_t agc_gain_baseline = 0;
    static int8_t fft_gain_baseline = 0;
    esp_csi_gain_ctrl_get_rx_gain(rx_ctrl, &agc_gain, &fft_gain);
    if (capture_frame_count < 100) {
        esp_csi_gain_ctrl_record_rx_gain(agc_gain, fft_gain);
    } else if (capture_frame_count == 100) {
        esp_csi_gain_ctrl_get_rx_gain_baseline(&agc_gain_baseline, &fft_gain_baseline);
#if CONFIG_FORCE_GAIN
        esp_csi_gain_ctrl_set_rx_force_gain(agc_gain_baseline, fft_gain_baseline);
        ESP_LOGD(TAG, "fft_force %d, agc_force %d", fft_gain_baseline, agc_gain_baseline);
#endif
    }
    esp_csi_gain_ctrl_get_gain_compensation(&compensate_gain, agc_gain, fft_gain);
    (void)compensate_gain;
#endif
    frame.agc_gain = agc_gain;
    frame.fft_gain = fft_gain;
    for (int index = 0; index < frame.len; index++) {
        frame.data[index] = (int16_t)(compensate_gain * info->buf[index]);
    }
    if (xQueueSend(csi_queue, &frame, 0) != pdTRUE) {
        capture_queue_drops++;
        portENTER_CRITICAL(&csi_state_mux);
        csi_callback_busy = false;
        portEXIT_CRITICAL(&csi_state_mux);
        return;
    }
    capture_frame_count++;
    portENTER_CRITICAL(&csi_state_mux);
    csi_callback_busy = false;
    portEXIT_CRITICAL(&csi_state_mux);
}

static void wifi_csi_init()
{
    csi_queue = xQueueCreate(CSI_QUEUE_LENGTH, sizeof(csi_frame_t));
    if (!csi_queue) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    if (xTaskCreate(csi_output_task, "csi_output", 4096, NULL, 2, NULL) != pdPASS) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    wifi_promiscuous_filter_t promiscuous_filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA,
    };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&promiscuous_filter));
    esp_err_t result = esp_wifi_set_promiscuous(true);
    ets_printf("PROMISCUOUS,result=%s\n", esp_err_to_name(result));
    ESP_ERROR_CHECK(result);

    /**< default config */
#if CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C61
    wifi_csi_config_t csi_config = {
        .enable                   = true,
        .acquire_csi_legacy       = false,
        .acquire_csi_force_lltf   = CSI_FORCE_LLTF,
        .acquire_csi_ht20         = true,
        .acquire_csi_ht40         = true,
        .acquire_csi_vht          = false,
        .acquire_csi_su           = false,
        .acquire_csi_mu           = false,
        .acquire_csi_dcm          = false,
        .acquire_csi_beamformed   = false,
        .acquire_csi_he_stbc_mode = 2,
        .val_scale_cfg            = 0,
        .dump_ack_en              = false,
        .reserved                 = false
    };
#elif CONFIG_IDF_TARGET_ESP32C6
    wifi_csi_config_t csi_config = {
        .enable                 = true,
        .acquire_csi_legacy     = false,
        .acquire_csi_ht20       = true,
        .acquire_csi_ht40       = true,
        .acquire_csi_su         = true,
        .acquire_csi_mu         = true,
        .acquire_csi_dcm        = true,
        .acquire_csi_beamformed = true,
        .acquire_csi_he_stbc    = 2,
        .val_scale_cfg          = false,
        .dump_ack_en            = false,
        .reserved               = false
    };
#else
    wifi_csi_config_t csi_config = {
        .lltf_en           = true,
        .htltf_en          = true,
        .stbc_htltf2_en    = true,
        .ltf_merge_en      = true,
        .channel_filter_en = true,
        .manu_scale        = false,
        .shift             = false,
    };
#endif
    result = esp_wifi_set_csi_config(&csi_config);
    ets_printf("CSI_CONFIG,result=%s\n", esp_err_to_name(result));
    ESP_ERROR_CHECK(result);
    result = esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, NULL);
    ets_printf("CSI_CALLBACK,result=%s\n", esp_err_to_name(result));
    ESP_ERROR_CHECK(result);
    result = esp_wifi_set_csi(true);
    ets_printf("CSI_ENABLE,result=%s\n", esp_err_to_name(result));
    ESP_ERROR_CHECK(result);
    csi_ready = true;
}

void receiver_init()
{
    ets_printf("Receiver startup\n");

    /**
     * @brief Initialize NVS
     */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ets_printf("NVS ready\n");

    /**
     * @brief Initialize Wi-Fi
     */
    wifi_init();
    ets_printf("WiFi ready on channel %d, HT20\n", CONFIG_LESS_INTERFERENCE_CHANNEL);
    for (int attempt = 0; attempt < 100 && !wifi_connected; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!wifi_connected) {
        ets_printf("WIFI_CONNECT_TIMEOUT,ssid=%s\n", CSI_TX_SSID);
    }
    refresh_transmitter_bssid();

    wifi_csi_init();
    ets_printf("CSI ready; waiting for UDP traffic from %s\n", CSI_TX_SSID);
}

