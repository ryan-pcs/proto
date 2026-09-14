#pragma once
#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// CSI capture.
//
// This is the student's receiver firmware, moved in from receiver/ so that the
// screen and the capture are one program on one board. receiver/ is kept as
// the audited reference copy; this is the living one. The differences are
// listed at the top of csi_capture.c.
//
// Nothing here draws anything or knows the screen exists. Rows come out
// through csi_sink.h; counters come out through the functions below, and
// run_control fills UiState from them.
// ---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

// Starts NVS, WiFi and CSI. Blocks for up to ten seconds waiting to join the
// transmitter's network, so call it where a wait is acceptable - and note it
// gives up rather than hanging forever if the transmitter is not switched on.
void csiCaptureInit(void);

// Arms capture. Clears the counters and empties the queue first, so every run
// starts from zero.
void csiCaptureStart(void);

// Disarms capture and waits for the queue to drain, up to three seconds.
// False means it was still busy at the end of that - the run should be treated
// as incomplete rather than trusted.
bool csiCaptureStop(void);

// Live counters, safe to read at any time.
uint32_t csiCaptureRows(void);    // rows handed to the sink this run
uint32_t csiCaptureDrops(void);   // rows lost because the queue was full

bool csiCaptureIsConnected(void); // joined the transmitter's network
bool csiCaptureIsReady(void);     // CSI is configured and running

// The CSV header matching the rows this build produces, without a newline.
// Whoever opens a destination writes this first - the serial sink at the
// start of a run, storage when it creates a capture file.
const char* csiCaptureHeaderLine(void);

#ifdef __cplusplus
}
#endif
