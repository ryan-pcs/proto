#include "driver/uart.h"
#include "rom/ets_sys.h"
#include <Arduino.h>
#include <string.h>

extern "C" void receiver_init();
extern "C" void receiver_capture_start();
extern "C" bool receiver_capture_stop();
extern "C" uint32_t receiver_capture_queue_drops();
extern "C" bool receiver_is_connected();
extern "C" bool receiver_is_ready();

static char command_buffer[32];
static size_t command_length = 0;

static void process_receiver_commands()
{
    while (Serial.available() > 0) {
        char character = static_cast<char>(Serial.read());
        if (character == '\r') {
            continue;
        }
        if (character == '\n') {
            command_buffer[command_length] = '\0';
            if (strcmp(command_buffer, "CSI_CAPTURE_ON") == 0) {
                receiver_capture_start();
                ets_printf("CSI_CAPTURE_READY\n");
            } else if (strcmp(command_buffer, "CSI_CAPTURE_OFF") == 0) {
                bool stopped = receiver_capture_stop();
                ets_printf(stopped ? "CSI_CAPTURE_STOPPED,drops=%lu\n" : "CSI_CAPTURE_STOP_TIMEOUT,drops=%lu\n",
                           static_cast<unsigned long>(receiver_capture_queue_drops()));
            } else if (strcmp(command_buffer, "CSI_STATUS") == 0) {
                ets_printf("RECEIVER_STATUS,ready=%d,wifi=%d\n",
                           receiver_is_ready() ? 1 : 0,
                           receiver_is_connected() ? 1 : 0);
            }
            command_length = 0;
        } else if (command_length < sizeof(command_buffer) - 1) {
            command_buffer[command_length++] = character;
        }
    }
}
void setup()
{
    Serial.begin(921600);
    uart_set_baudrate(UART_NUM_0, 921600);
    receiver_init();
}

void loop()
{
    process_receiver_commands();
    delay(10);
}
