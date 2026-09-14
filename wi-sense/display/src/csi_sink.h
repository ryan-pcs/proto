#pragma once
#include <stddef.h>

// ---------------------------------------------------------------------------
// Where a finished CSI row goes.
//
// THIS FILE IS THE OTHER SEAM. ui_state.h decides what the screen shows;
// this decides where the readings are written.
//
// The capture code formats one complete row of text and hands it here. It
// never knows, and must never care, whether that row ends up on the serial
// cable to a laptop or in a file on the memory card. Swapping one for the
// other is a call to csiSinkSet() and nothing else.
//
// The text handed over is byte-for-byte the same CSV the project has always
// produced, so whatever is on the other end - the Python tools, a card file,
// a monitor window - reads it the same way it always has.
// ---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

// One row of CSV, without its newline. len is the length, so a sink never has
// to walk the string to find the end.
typedef void (*CsiRowSink)(const char* line, size_t len);

// Install the sink. Passing NULL means rows are formatted and then dropped,
// which is what happens before anything has been chosen.
void       csiSinkSet(CsiRowSink sink);
CsiRowSink csiSinkGet(void);

// The built-in sink: writes to Serial.
//
// Deliberately Arduino's Serial and not ets_printf, which the capture code
// used when it ran alone. On the S3 those are not the same place - ets_printf
// always goes to UART0 while Serial follows the USB build flags - so using
// Serial keeps the readings on the same port as every other message this
// program prints, and keeps the laptop tools working against one port.
void csiSinkSerial(const char* line, size_t len);

#ifdef __cplusplus
}
#endif
