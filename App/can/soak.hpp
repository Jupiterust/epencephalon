/*
 * soak.hpp - the sustained-load run.
 *
 * What it is for (docs/CAN协议.md section 0): the second half of the P0-1 fix
 * -- "the caller enters a critical section while modifying the ring" -- is
 * structurally unverifiable on the host. LoopbackBus has no critical section
 * and the host has no real interrupt, so the injector and the lock sit on the
 * same side and only ever agree with themselves. Sustained real traffic with a
 * real interrupt is the only place that half gets exercised.
 *
 * It also carries the 32-bit millisecond wraparound: uwTick is set to
 * 0xFFFF0000 and the run continues across the rollover.
 *
 * Progress goes into g_soak, a plain .bss struct, as well as the report. The
 * report ring wraps long before a 30-minute run ends, so the struct is what a
 * debugger reads; the ring is for the serial log.
 */
#pragma once

#include <cstdint>

#include "can_bus.hpp"

namespace app::can {

struct SoakState {
    uint32_t cycles = 0;
    uint32_t elapsedMs = 0;
    uint32_t targetMs = 0;
    uint32_t sent = 0;
    uint32_t received = 0;
    uint32_t txRejected = 0;       /* send() refused: ring full */

    /* The six counters that must stay at zero. */
    uint32_t rxRingOverflow = 0;
    uint32_t rxFifoOverrun = 0;
    uint32_t txRingOverflow = 0;
    uint32_t txAborted = 0;
    uint32_t strayReplies = 0;
    uint32_t duplicateReplies = 0;

    uint32_t maxPollCycles = 0;
    uint32_t maxPeriodUs = 0;      /* worst observed cadence, 3000 us nominal */
    uint32_t minPeriodUs = 0xFFFFFFFF;

    uint32_t tickAtStart = 0;
    uint8_t  phase = 0;            /* 0 idle, 1 normal, 2 wraparound */
    uint8_t  wrapped = 0;          /* the rollover was actually observed */
    uint8_t  done = 0;
    uint8_t  clean = 0;            /* all six counters still zero */
};

extern SoakState g_soak;

/* Blocks for the whole run. phase 1 then phase 2. */
void runSoak(uint32_t normalMs, uint32_t wrapMs);

}  // namespace app::can
