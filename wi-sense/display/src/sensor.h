#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// The laser distance sensor.
//
// A thin wrapper around the laser_sensor library in lib/. That library is
// written against the chip's own low-level drivers; everything here keeps
// that contained, so the rest of the program only ever sees a distance in
// millimetres and a yes-or-no about whether the sensor is working.
//
// Its job in the finished machine is to notice that a bag has been put in the
// tray, so the machine only scans when there is something to scan.
// ---------------------------------------------------------------------------

struct SensorInfo {
    bool     present;        // the sensor answered when asked
    bool     inRange;        // the last reading was a real distance
    uint16_t distanceMm;     // the last reading
    char     error[40];      // why it failed, in plain words
};

// Sets up the two-wire connection and the sensor. False if it does not answer.
bool sensorBegin(SensorInfo& info);

// Takes one reading. Blocks for about four milliseconds. False if the reading
// failed or nothing was in range.
bool sensorRead(SensorInfo& info);
