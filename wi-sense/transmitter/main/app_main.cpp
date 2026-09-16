/*
 * UDP CSI Transmitter
 * Arduino Framework Version for ESP32 WROOM 32
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <string.h>
#include <esp_wifi.h>

// esp_wifi_internal_set_fix_rate() is compiled into libnet80211.a, but this
// Arduino framework does not ship esp_wifi_internal.h (checked 2026-09-16:
// the header is absent, the symbol is present in the archive). Declaring the
// prototype is the supported way to reach it from Arduino; the signature is
// the one ESP-IDF has carried since v4.x.
extern "C" esp_err_t esp_wifi_internal_set_fix_rate(
    wifi_interface_t ifx, bool en, wifi_phy_rate_t rate);

#define CONFIG_LESS_INTERFERENCE_CHANNEL   11
// Every frame leaves at this rate, with link adaptation switched off. See the
// note in wifi_init() for why. LGI, not SGI, because every frame the receiver
// has ever logged carries sgi=0.
//
// MCS0 (BPSK 1/2), not MCS7. Measured 2026-09-16 with the metal sheet on the
// stand: MCS7 lost 130-258 of ~370 packets per run and every run came back
// "partial". RSSI was only -47.9 dBm, far above MCS7's nominal sensitivity, so
// this is not a power problem - the sheet carves deep frequency-selective
// nulls into the band and 64-QAM 5/6 has no coding margin to ride through
// them. MCS0 has the most.
//
// Throughput is irrelevant here: one small UDP datagram every 50 ms at 20 Hz.
// The CSI itself is unaffected by the choice, because it is measured from the
// HT-LTF preamble, which is identical at every MCS.
#define CONFIG_FIXED_PHY_RATE              WIFI_PHY_RATE_MCS0_LGI
#define CONFIG_DEFAULT_SEND_FREQUENCY      100
#define CONFIG_DEFAULT_BURST_DURATION_MS   5000
#define CONFIG_MAX_BURST_DURATION_MS       60000
#define CONFIG_MAX_SEND_FREQUENCY          1000

static bool burst_active = false;
static uint32_t burst_started_at = 0;
static uint32_t burst_duration_ms = 0;
static uint16_t burst_frequency_hz = 0;
static uint32_t burst_packet_count = 0;
static volatile uint32_t burst_success_count = 0;
static volatile uint32_t burst_fail_count = 0;
static uint32_t next_send_at_us = 0;
static bool wifi_initialized = false;
static char command_buffer[80];
static size_t command_length = 0;
static WiFiUDP udp;
static const IPAddress CSI_RECEIVER(192, 168, 4, 2);
static const uint16_t CSI_PORT = 4210;

void print_status() {
    Serial.print("STATUS,");
    Serial.print(!wifi_initialized ? "FAULT" : (burst_active ? "RUNNING" : "IDLE"));
    Serial.print(",packets=");
    Serial.print(burst_packet_count);
    Serial.print(",success=");
    Serial.print(burst_success_count);
    Serial.print(",fail=");
    Serial.print(burst_fail_count);
    Serial.println(",mode=udp_ap");
}

void stop_burst() {
    if (!burst_active) {
        Serial.println("BURST_STOPPED,packets=0");
        return;
    }

    burst_active = false;
    Serial.print("BURST_END,packets=");
    Serial.print(burst_packet_count);
    Serial.print(",success=");
    Serial.print(burst_success_count);
    Serial.print(",fail=");
    Serial.println(burst_fail_count);
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
    burst_success_count = 0;
    burst_fail_count = 0;
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
        if (!wifi_initialized) {
            Serial.println("ERROR,wifi_not_ready");
            return;
        }
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

bool wifi_init() {
    WiFi.mode(WIFI_AP);

    // Re-initialise the WiFi driver with TX aggregation OFF.
    //
    // A fixed PHY rate cannot be set while TX AMPDU is enabled - the stack
    // refuses with "can't set fix rate when tx ampdu is enabled" (measured on
    // this board, 2026-09-16). ampdu_tx_enable is a runtime field of
    // wifi_init_config_t, but Arduino's WiFi.mode() calls esp_wifi_init() with
    // an unmodified WIFI_INIT_CONFIG_DEFAULT() and exposes no hook for it
    // (WiFiGeneric.cpp, wifiLowLevelInit). Editing the framework package would
    // not live in this repository and would vanish on the next update, so the
    // driver is stopped and re-initialised here instead.
    //
    // The esp_netif objects Arduino created in WiFi.mode() are not destroyed by
    // esp_wifi_deinit(), so this replaces the driver's configuration only and
    // leaves the network interfaces and the UDP stack intact.
    //
    // Aggregation costs this firmware nothing: it sends one small UDP datagram
    // every 50 ms at 20 Hz, so there is never a second frame waiting to
    // aggregate with.
    esp_wifi_stop();
    esp_wifi_deinit();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.ampdu_tx_enable = 0;
    esp_err_t reinit = esp_wifi_init(&cfg);
    Serial.print("AMPDU_TX_OFF,result=");
    Serial.println(reinit == ESP_OK ? "ESP_OK" : esp_err_to_name(reinit));
    if (reinit != ESP_OK) {
        return false;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_AP);

    if (!WiFi.softAPConfig(
        IPAddress(192, 168, 4, 1),
        IPAddress(192, 168, 4, 1),
        IPAddress(255, 255, 255, 0)
    )) {
        return false;
    }
    if (!WiFi.softAP("CSI_TX", "csi12345", CONFIG_LESS_INTERFERENCE_CHANNEL, false, 1)) {
        return false;
    }
    if (!udp.begin(CSI_PORT)) {
        return false;
    }

    // Start the driver explicitly. WiFi.softAP() normally does this, but it
    // skips the call because Arduino's own _esp_wifi_started flag still reads
    // true from before the esp_wifi_stop() above - it tracks Arduino's calls,
    // not the driver. Without this the next call returns
    // ESP_ERR_WIFI_NOT_STARTED. Starting an already-started driver is a no-op.
    esp_err_t started = esp_wifi_start();
    if (started != ESP_OK && started != ESP_ERR_WIFI_NOT_STOPPED) {
        Serial.print("WIFI_START,result=");
        Serial.println(esp_err_to_name(started));
        return false;
    }

    // Pin the physical rate and switch link adaptation off.
    //
    // Measured 2026-09-16: without this the stack spreads a single 20 s run
    // across MCS 3-7. Two frame sets from the SAME run, so the same channel,
    // differ by 0.017 - about the whole run-to-run noise floor (0.004-0.023).
    //
    // On its own that is only noise. The danger is direction: metal in the
    // path drops the link by several dB, adaptation answers by changing rate,
    // and the MCS mix then shifts WITH the condition. That turns a rate
    // artefact into a bias pointing the same way as the effect being
    // measured, which is the one failure that would make a positive result
    // worthless. Filtering by MCS afterwards cannot undo it either, because
    // adaptation picks the rate from instantaneous channel quality, so the
    // surviving frames are a biased sample.
    //
    // Failure here is fatal on purpose. Silently collecting adapted frames
    // looks exactly like a good session and would not be caught until the
    // analysis, or never.
    esp_err_t fixed_rate = esp_wifi_internal_set_fix_rate(
        WIFI_IF_AP, true, CONFIG_FIXED_PHY_RATE);
    Serial.print("FIX_RATE,rate=MCS0_LGI,result=");
    Serial.println(fixed_rate == ESP_OK ? "ESP_OK" : esp_err_to_name(fixed_rate));
    if (fixed_rate != ESP_OK) {
        return false;
    }

    Serial.print("WiFi Initialized on channel: ");
    Serial.println(CONFIG_LESS_INTERFERENCE_CHANNEL);
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n================ CSI SEND ================");
    Serial.println("Initializing ESP32 for UDP CSI transmission");

    wifi_initialized = wifi_init();
    if (!wifi_initialized) {
        Serial.println("ERROR,wifi_initialization_failed");
        return;
    }
    delay(100);
    Serial.println("READY,commands=START duration_ms rate_hz|STOP|STATUS,max_duration_ms=60000,max_rate_hz=1000,mode=udp_ap");
}

void loop() {
    process_serial_commands();

    if (burst_active && millis() - burst_started_at >= burst_duration_ms) {
        stop_burst();
    }

    if (burst_active && (int32_t)(micros() - next_send_at_us) >= 0) {
        uint32_t sequence = burst_packet_count;
        int begin_result = udp.beginPacket(CSI_RECEIVER, CSI_PORT);
        size_t written = begin_result == 1
            ? udp.write((const uint8_t *)&sequence, sizeof(sequence))
            : 0;
        int result = (begin_result == 1 && written == sizeof(sequence))
            ? udp.endPacket()
            : 0;
        if (result == 1) {
            burst_success_count++;
        } else {
            burst_fail_count++;
        }
        if (begin_result != 1 || written != sizeof(sequence) || result != 1) {
            Serial.print("ERROR,send=");
            Serial.print(result);
            Serial.print(",begin=");
            Serial.print(begin_result);
            Serial.print(",write=");
            Serial.println(written);
        }
        burst_packet_count++;
        next_send_at_us = micros() + 1000000UL / burst_frequency_hz;
    }

    delay(1);
}
