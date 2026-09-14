#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include <stdio.h>

#include "sensor.h"
#include "board_pins.h"

// ---------------------------------------------------------------------------
// The sensor is talked to directly here rather than through the library in
// lib/laser_sensor. That library is written against the chip's low-level
// drivers, which this Arduino-based project does not expose.
//
// Nothing is lost by doing it this way: the library's conversation with the
// sensor is short, and it is reproduced faithfully below — including the one
// millisecond pause it documents as being required between asking for a
// register and reading it.
//
// The register numbers, the expected identity value and the out-of-range
// reading all come from that library.
// ---------------------------------------------------------------------------

static const uint8_t  LASER_ADDR      = 0x26;   // fixed, cannot be changed
static const uint8_t  REG_DEVICE_ID   = 0x01;   // reads back 0x47 when alive
static const uint8_t  REG_CONFIG      = 0x02;   // 0x04 resets, 0x02 enables
static const uint8_t  REG_DISTANCE_H  = 0x03;
static const uint8_t  REG_DISTANCE_L  = 0x04;

static const uint8_t  DEVICE_ID_VALUE = 0x47;
static const uint16_t OUT_OF_RANGE    = 8191;   // what it reports when it sees nothing
static const uint32_t I2C_HZ          = 100000; // the sensor is slow; keep it slow

static void setError(SensorInfo& info, const char* why) {
    strncpy(info.error, why, sizeof(info.error) - 1);
    info.error[sizeof(info.error) - 1] = '\0';
}

// Ask for a register, stop, pause, then read the answer. The stop and the
// pause are not optional — this sensor will not answer a combined request.
static bool readReg(uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(LASER_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) return false;

    delay(1);

    if (Wire.requestFrom((int)LASER_ADDR, 1) != 1) return false;
    value = Wire.read();
    return true;
}

static bool writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(LASER_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission(true) == 0;
}

bool sensorBegin(SensorInfo& info) {
    memset(&info, 0, sizeof(info));

    Wire.begin(PIN_TOF_SDA, PIN_TOF_SCL, I2C_HZ);

    uint8_t id = 0;
    if (!readReg(REG_DEVICE_ID, id)) {
        setError(info, "no answer on the two wires");
        return false;
    }
    if (id != DEVICE_ID_VALUE) {
        snprintf(info.error, sizeof(info.error), "wrong sensor, id 0x%02X", id);
        return false;
    }

    if (!writeReg(REG_CONFIG, 0x04)) {      // reset
        setError(info, "reset was refused");
        return false;
    }
    delay(500);                              // it needs this long to come back

    if (!writeReg(REG_CONFIG, 0x02)) {      // enable
        setError(info, "enable was refused");
        return false;
    }

    info.present = true;
    return true;
}

bool sensorRead(SensorInfo& info) {
    if (!info.present) {
        setError(info, "sensor not started");
        return false;
    }

    uint8_t cfg = 0;
    if (!readReg(REG_CONFIG, cfg)) {
        info.inRange = false;
        setError(info, "reading failed");
        return false;
    }

    // Bottom bit set means it looked and saw nothing.
    if (cfg & 0x01) {
        info.distanceMm = OUT_OF_RANGE;
        info.inRange = false;
        setError(info, "nothing in range");
        return false;
    }

    uint8_t high = 0, low = 0;
    if (!readReg(REG_DISTANCE_H, high) || !readReg(REG_DISTANCE_L, low)) {
        info.inRange = false;
        setError(info, "reading failed");
        return false;
    }

    info.distanceMm = (uint16_t)((high << 8) | low);
    info.inRange = (info.distanceMm != OUT_OF_RANGE);

    if (!info.inRange) {
        setError(info, "nothing in range");
        return false;
    }

    info.error[0] = '\0';
    return true;
}
