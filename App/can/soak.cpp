#include "soak.hpp"

#include "can_config.hpp"
#include "report.hpp"

#include "can_master.hpp"
#include "protocol.hpp"

#include "main.h"
#include "can.h"
#include "cmsis_os.h"

#include <cstdio>

namespace app::can {

SoakState g_soak;

namespace {

constexpr uint32_t kPeriodMs = 3;          /* the control cadence */
constexpr uint32_t kFramesPerCycle = 2;    /* two setpoint-shaped frames per tick */
constexpr uint32_t kProgressMs = 10000;    /* one report line every 10 s */

/* CanMaster is constructed in place, same reason as in selftest.cpp: it takes
 * the bus by reference and the bus must be started first. */
alignas(jm::master::CanMaster) unsigned char g_masterStore[sizeof(jm::master::CanMaster)];
jm::master::CanMaster *g_master = nullptr;

jm::master::CanFrame makeTraffic(uint8_t node, uint32_t seq)
{
    jm::master::CanFrame f{};
    /* SetInputPos is the highest-rate command in the real system, so the
     * identifier and length here match what the bus will actually carry. */
    f.id = jm::comm::frameId(node, static_cast<uint32_t>(jm::comm::CanCmd::SetInputPos));
    f.dlc = 8;
    /* Distinct non-zero bytes carrying the sequence, so a frame that arrives
     * with the right id but the wrong body is still caught. */
    for (int i = 0; i < 8; ++i) {
        f.data[i] = static_cast<uint8_t>((seq >> (i * 3)) ^ (0xA5 + i));
    }
    return f;
}

/* Baselines taken when the run starts. The self-test deliberately overflows
 * the receive ring (T08) and aborts a mailbox (T07b), so absolute counters
 * arrive here already non-zero; what this run is about is whether they move
 * from here. Reporting absolutes made the first validation run look dirty when
 * nothing had gone wrong. */
jm::master::BusDiagnostics g_base{};
uint32_t g_baseStray = 0;
uint32_t g_baseDup = 0;
uint32_t g_baseReceived = 0;

void takeBaseline()
{
    g_base = busCan1.diagnostics();
    if (g_master != nullptr) {
        g_baseStray = g_master->stats().strayReplies;
        g_baseDup = g_master->stats().duplicateReplies;
        g_baseReceived = g_master->stats().received;
    }
}

void snapshotCounters()
{
    const auto d = busCan1.diagnostics();
    g_soak.rxRingOverflow = d.rxRingOverflow - g_base.rxRingOverflow;
    g_soak.rxFifoOverrun = d.rxFifoOverrun - g_base.rxFifoOverrun;
    g_soak.txRingOverflow = d.txRingOverflow - g_base.txRingOverflow;
    g_soak.txAborted = d.txAborted - g_base.txAborted;
    if (g_master != nullptr) {
        const auto &st = g_master->stats();
        g_soak.strayReplies = st.strayReplies - g_baseStray;
        g_soak.duplicateReplies = st.duplicateReplies - g_baseDup;
        /* poll() consumes the frames, so the master's own count is the only
         * honest source. An earlier version drained the bus after poll() and
         * always read zero -- it was counting what poll() had already taken. */
        g_soak.received = st.received - g_baseReceived;
    }

    /* In pure self-loopback every frame we transmit comes back, matches no
     * outstanding request, and is booked as a stray reply. That is the test
     * rig, not a defect: strayReplies cannot be zero here. What must hold is
     * that it tracks reception exactly -- one stray per frame received, none
     * lost and none invented. On a real bus with real slaves the criterion
     * goes back to zero. */
    const bool strayIsLoopbackArtefact = (g_soak.strayReplies == g_soak.received);
    g_soak.clean = (g_soak.rxRingOverflow == 0 && g_soak.rxFifoOverrun == 0 &&
                    g_soak.txRingOverflow == 0 && g_soak.txAborted == 0 &&
                    g_soak.duplicateReplies == 0 && strayIsLoopbackArtefact) ? 1 : 0;
}

void emitProgress(const char *tag)
{
    report.emit("SOAK %s phase=%u t=%lus cyc=%lu tx=%lu rx=%lu rej=%lu "
                "rxRingOvf=%lu rxFifoOvf=%lu txRingOvf=%lu txAbort=%lu stray=%lu dup=%lu "
                "poll_max=%luns period=%lu..%luus wrapped=%u\r\n",
                tag, g_soak.phase,
                static_cast<unsigned long>(g_soak.elapsedMs / 1000),
                static_cast<unsigned long>(g_soak.cycles),
                static_cast<unsigned long>(g_soak.sent),
                static_cast<unsigned long>(g_soak.received),
                static_cast<unsigned long>(g_soak.txRejected),
                static_cast<unsigned long>(g_soak.rxRingOverflow),
                static_cast<unsigned long>(g_soak.rxFifoOverrun),
                static_cast<unsigned long>(g_soak.txRingOverflow),
                static_cast<unsigned long>(g_soak.txAborted),
                static_cast<unsigned long>(g_soak.strayReplies),
                static_cast<unsigned long>(g_soak.duplicateReplies),
                static_cast<unsigned long>(Report::cyclesToNs(g_soak.maxPollCycles)),
                static_cast<unsigned long>(g_soak.minPeriodUs),
                static_cast<unsigned long>(g_soak.maxPeriodUs),
                g_soak.wrapped);
}

/* One phase of the run. Returns when durationMs of wall time has passed. */
void runPhase(uint8_t phase, uint32_t durationMs)
{
    g_soak.phase = phase;
    g_soak.elapsedMs = 0;
    g_soak.targetMs = durationMs;
    g_soak.minPeriodUs = 0xFFFFFFFF;
    g_soak.maxPeriodUs = 0;

    /* Wall time comes from the FreeRTOS tick, deliberately NOT from
     * HAL_GetTick(): phase 2 moves uwTick to just below the 32-bit rollover,
     * so a loop bounded by HAL_GetTick would end at a moment decided by the
     * thing under test. */
    const uint32_t startTick = osKernelGetTickCount();
    uint32_t nextWake = startTick;
    uint32_t lastProgress = startTick;
    uint32_t lastCycleCycles = Report::cycles();
    uint32_t seq = 0;
    uint32_t phaseCycle = 0;
    uint32_t lastHalTick = HAL_GetTick();

    for (;;) {
        const uint32_t nowTick = osKernelGetTickCount();
        g_soak.elapsedMs = nowTick - startTick;
        if (g_soak.elapsedMs >= durationMs) { break; }

        /* Cadence measurement, in DWT cycles so it is independent of both
         * tick sources. */
        const uint32_t nowCycles = Report::cycles();
        const uint32_t periodUs = Report::cyclesToNs(nowCycles - lastCycleCycles) / 1000u;
        lastCycleCycles = nowCycles;
        if (phaseCycle > 2) {             /* the first samples of a phase have no valid predecessor */
            if (periodUs > g_soak.maxPeriodUs) { g_soak.maxPeriodUs = periodUs; }
            if (periodUs < g_soak.minPeriodUs) { g_soak.minPeriodUs = periodUs; }
        }

        /* Offer traffic. send() is fire-and-forget; a refusal means the ring
         * was full and is counted rather than retried, because a retried
         * setpoint is a stale setpoint. */
        for (uint32_t i = 0; i < kFramesPerCycle; ++i) {
            const uint8_t node = static_cast<uint8_t>(seq & 0x3u);   /* nodes 0..3 */
            if (busCan1.send(makeTraffic(node, seq))) { ++g_soak.sent; }
            else { ++g_soak.txRejected; }
            ++seq;
        }

        const uint32_t t0 = Report::cycles();
        g_master->poll();
        const uint32_t pollCycles = Report::cycles() - t0;
        if (pollCycles > g_soak.maxPollCycles) { g_soak.maxPollCycles = pollCycles; }

        /* poll() has already drained the bus into the master; anything left
         * here would be a frame the master chose not to take. */

        /* Did HAL's millisecond counter roll over during this phase? */
        const uint32_t halTick = HAL_GetTick();
        if (halTick < lastHalTick) { g_soak.wrapped = 1; }
        lastHalTick = halTick;

        ++g_soak.cycles;
        ++phaseCycle;
        snapshotCounters();

        if (nowTick - lastProgress >= pdMS_TO_TICKS(kProgressMs)) {
            lastProgress = nowTick;
            emitProgress("PROG");
            report.service();
        }

        nextWake += pdMS_TO_TICKS(kPeriodMs);
        osDelayUntil(nextWake);
    }
    emitProgress("PHASE_END");
}

}  // namespace

void runSoak(uint32_t normalMs, uint32_t wrapMs)
{
    if (g_master == nullptr) {
        g_master = new (g_masterStore) jm::master::CanMaster(busCan1);
        for (uint8_t n = 0; n <= jm::master::kHighestNode; ++n) { g_master->addJoint(n); }
    }
    g_soak = SoakState{};
    g_soak.tickAtStart = HAL_GetTick();
    takeBaseline();

    report.emit("SOAK BEGIN normal=%lums wrap=%lums period=%lums frames/cycle=%lu\r\n",
                static_cast<unsigned long>(normalMs), static_cast<unsigned long>(wrapMs),
                static_cast<unsigned long>(kPeriodMs),
                static_cast<unsigned long>(kFramesPerCycle));
    runPhase(1, normalMs);

    /* Phase 2: park HAL's millisecond counter just below the 32-bit rollover.
     * uwTick is HAL's own variable (stm32f4xx_hal.h:204) and HAL_GetTick()
     * returns it unchanged (stm32f4xx_hal.c:323-326), so this really does move
     * the clock BxCanBus::nowMs() reads -- nothing is stubbed.
     *
     * 0xFFFF0000 is 65536 ms short of the wrap, so the rollover lands about a
     * minute into this phase and the rest of it runs past the discontinuity. */
    report.emit("SOAK WRAP set uwTick=0xFFFF0000 (rollover in %lums)\r\n",
                static_cast<unsigned long>(0u - 0xFFFF0000u));
    __disable_irq();
    uwTick = 0xFFFF0000u;
    __enable_irq();
    runPhase(2, wrapMs);

    g_soak.done = 1;
    report.emit("SOAK END clean=%u wrapped=%u cycles=%lu sent=%lu recv=%lu\r\n",
                g_soak.clean, g_soak.wrapped,
                static_cast<unsigned long>(g_soak.cycles),
                static_cast<unsigned long>(g_soak.sent),
                static_cast<unsigned long>(g_soak.received));
}

}  // namespace app::can
