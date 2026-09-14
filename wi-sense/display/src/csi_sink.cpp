#include <Arduino.h>

#include "csi_sink.h"

static CsiRowSink s_sink = nullptr;

void csiSinkSet(CsiRowSink sink) {
    s_sink = sink;
}

CsiRowSink csiSinkGet(void) {
    return s_sink;
}

void csiSinkSerial(const char* line, size_t len) {
    if (!line) return;
    Serial.write(reinterpret_cast<const uint8_t*>(line), len);
    Serial.write('\n');
}
