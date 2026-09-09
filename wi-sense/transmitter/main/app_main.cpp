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
#define CONFIG_SEND_FREQUENCY              100

static const uint8_t CONFIG_CSI_SEND_MAC[] = {0x1a, 0x00, 0x00, 0x00, 0x00, 0x00};
static esp_now_peer_info_t peer;
static uint32_t send_count = 0;
static unsigned long last_send_time = 0;

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

    Serial.print("Send frequency: ");
    Serial.print(CONFIG_SEND_FREQUENCY);
    Serial.println(" Hz");
    Serial.print("MAC: ");
    for (int i = 0; i < 6; i++) {
        if (CONFIG_CSI_SEND_MAC[i] < 16) Serial.print("0");
        Serial.print(CONFIG_CSI_SEND_MAC[i], HEX);
        if (i < 5) Serial.print(":");
    }
    Serial.println();

    last_send_time = millis();
}

void loop() {
    unsigned long current_time = millis();

    if (current_time - last_send_time >= (1000 / CONFIG_SEND_FREQUENCY)) {
        esp_err_t result = esp_now_send(peer.peer_addr, (const uint8_t *)&send_count, sizeof(send_count));

        if (result != ESP_OK) {
            Serial.print("ESP-NOW send error: ");
            Serial.println(result);
        } else {
            Serial.print("Sent packet #");
            Serial.println(send_count);
        }

        send_count++;
        last_send_time = current_time;
    }

    delay(1);
}
