#include "driver/uart.h"

extern "C" void receiver_init();

void setup()
{
    uart_set_baudrate(UART_NUM_0, 115200);
    receiver_init();
}

void loop()
{
}
