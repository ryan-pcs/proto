/*
 * ESP-NOW CSI Transmitter
 * Arduino Framework Version for ESP32 WROOM 32
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#define CONFIG_LESS_INTERFERENCE_CHANNEL   11
#define CONFIG_ESP_NOW_RATE                WIFI_PHY_RATE_MCS0_LGI
#define CONFIG_DEFAULT_SEND_FREQUENCY      100
#define CONFIG_DEFAULT_BURST_DURATION_MS   5000
#define CONFIG_MAX_BURST_DURATION_MS       600000
#define CONFIG_MAX_SEND_FREQUENCY          1000

static const uint8_t CONFIG_CSI_SEND_MAC[] = {0xc6, 0x96, 0xce, 0x25, 0xa0, 0x09};
static esp_now_peer_info_t peer;
static bool burst_active = false;
static uint32_t burst_started_at = 0;
static uint32_t burst_duration_ms = 0;
static uint16_t burst_frequency_hz = 0;
static uint32_t burst_packet_count = 0;
static uint32_t next_send_at_us = 0;
static char command_buffer[80];
static size_t command_length = 0;

void print_status() {
    Serial.print("STATUS,");
    Serial.print(burst_active ? "RUNNING" : "IDLE");
    Serial.print(",packets=");
    Serial.println(burst_packet_count);
}

void stop_burst() {
    if (!burst_active) {
        Serial.println("BURST_STOPPED,packets=0");
        return;
    }

    burst_active = false;
    Serial.print("BURST_END,packets=");
    Serial.println(burst_packet_count);
}

void start_burst(uint32_t duration_ms, uint16_t frequency_hz) {
    if (duration_ms == 0 || duration_ms > CONFIG_MAX_BURST_DURATION_MS ||
        frequency_hz == 0 || frequency_hz > CONFIG_MAX_SEND_FREQUENCY) {
        Serial.println("ERROR,invalid_burst_parameters");
        return;
    }

    burst_active = true;
    burst_started_at = millis();
    burst_duration_ms = duration_ms;
    burst_frequency_hz = frequency_hz;
    burst_packet_count = 0;
    next_send_at_us = micros();

    Serial.print("BURST_START,duration_ms=");
    Serial.print(duration_ms);
    Serial.print(",rate_hz=");
    Serial.println(frequency_hz);
}

void handle_command(const char *command) {
    unsigned long duration_ms = 0;
    unsigned int frequency_hz = 0;

    if (strncmp(command, "START", 5) == 0) {
        int values = sscanf(command, "START %lu %u", &duration_ms, &frequency_hz);
        if (values == 0) {
            duration_ms = CONFIG_DEFAULT_BURST_DURATION_MS;
            frequency_hz = CONFIG_DEFAULT_SEND_FREQUENCY;
        } else if (values != 2) {
            Serial.println("ERROR,use_START_duration_ms_rate_hz");
            return;
        }
        if (burst_active) {
            stop_burst();
        }
        start_burst((uint32_t)duration_ms, (uint16_t)frequency_hz);
    } else if (strcmp(command, "STOP") == 0) {
        stop_burst();
    } else if (strcmp(command, "STATUS") == 0) {
        print_status();
    } else if (command[0] != '\0') {
        Serial.println("ERROR,unknown_command");
    }
}

void process_serial_commands() {
    while (Serial.available() > 0) {
        char character = (char)Serial.read();
        if (character == '\r') {
            continue;
        }
        if (character == '\n') {
            command_buffer[command_length] = '\0';
            handle_command(command_buffer);
            command_length = 0;
        } else if (command_length < sizeof(command_buffer) - 1) {
            command_buffer[command_length++] = character;
        }
    }
}

void wifi_init() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_mac(WIFI_IF_STA, CONFIG_CSI_SEND_MAC);
    esp_wifi_set_ps(WIFI_PS_NONE);

    Serial.print("WiFi Initialized on channel: ");
    Serial.println(CONFIG_LESS_INTERFERENCE_CHANNEL);
}

void wifi_esp_now_init() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_set_pmk((uint8_t *)"pmk1234567890123");
    memset(&peer, 0, sizeof(peer));
    peer.channel = CONFIG_LESS_INTERFERENCE_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    memset(peer.peer_addr, 0xff, 6);

    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("Failed to add broadcast peer");
    }

    Serial.println("ESP-NOW Initialized");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n================ CSI SEND ================");
    Serial.println("Initializing ESP32 for ESP-NOW CSI transmission");

    wifi_init();
    delay(100);
    wifi_esp_now_init();

    Serial.print("MAC: ");
    for (int i = 0; i < 6; i++) {
        if (CONFIG_CSI_SEND_MAC[i] < 16) Serial.print("0");
        Serial.print(CONFIG_CSI_SEND_MAC[i], HEX);
        if (i < 5) Serial.print(":");
    }
    Serial.println();
    Serial.println("READY,commands=START duration_ms rate_hz|STOP|STATUS");
}

void loop() {
    process_serial_commands();

    if (burst_active && millis() - burst_started_at >= burst_duration_ms) {
        stop_burst();
    }

    if (burst_active && (int32_t)(micros() - next_send_at_us) >= 0) {
        uint32_t sequence = burst_packet_count;
        esp_err_t result = esp_now_send(peer.peer_addr, (const uint8_t *)&sequence, sizeof(sequence));
        if (result != ESP_OK) {
            Serial.print("ERROR,send=");
            Serial.println(result);
        }
        burst_packet_count++;
        next_send_at_us += 1000000UL / burst_frequency_hz;
    }

    delay(1);
}
