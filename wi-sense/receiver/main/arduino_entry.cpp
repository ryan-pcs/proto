#include "driver/uart.h"
#include <Arduino.h>

extern "C" void receiver_init();
extern "C" bool receiver_is_connected();
extern "C" bool receiver_is_ready();

void setup()
{
    Serial.begin(921600);
    uart_set_baudrate(UART_NUM_0, 921600);
    receiver_init();
}

void loop()
{
    static unsigned long last_ready_at = 0;
    if (receiver_is_ready() && receiver_is_connected() && millis() - last_ready_at >= 1000) {
        Serial.print("RECEIVER_READY,ssid=CSI_TX,csi=enabled,wifi=");
        Serial.println("connected");
        last_ready_at = millis();
    }
}
