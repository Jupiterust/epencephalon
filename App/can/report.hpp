/*
 * report.hpp - machine-readable self-test output on UART7 @ 921600.
 *
 * Same debug UART and baud rate as the joint driver boards, so one log parser
 * works on both sides.
 *
 * ## Printing must not perturb what is being measured
 *
 * During the DWT timing runs and the 30-minute soak, the print path is part
 * of the load. Two consequences, both deliberate:
 *
 *   1. Output is queued into a ring and pushed out by DMA. emit() copies into
 *      the ring and returns; it never waits for the wire. At 921600 baud one
 *      80-char line is ~870 us, which is a quarter of a 3 ms control period --
 *      blocking on that would change the numbers it is reporting.
 *
 *   2. emit() measures its own cost in DWT cycles and keeps the running
 *      max/total. selftest prints those as PERF report_cycles, so the reader
 *      can subtract the instrument from the measurement instead of trusting
 *      that it is negligible.
 *
 * If the ring overflows the count is reported rather than the line silently
 * vanishing: a self-test whose output can disappear is not evidence.
 */
#pragma once

#include <cstdarg>
#include <cstdint>

namespace app::can {

class Report
{
public:
    /* Overrides the CubeMX baud rate (usart.c has 115200) at runtime, so
     * changing it does not need a regeneration cycle. Also starts DWT. */
    bool init();

    /* printf-style, queued not blocking. Returns false if the line did not
     * fit -- counted, never silent. */
    bool emit(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

    /* Pushes whatever is queued. Call from the reporting task, never from a
     * measured path. */
    void service();

    /* Blocks until the ring is empty. Only for the end of a run. */
    void drain(uint32_t timeout_ms);

    uint32_t dropped()      const { return dropped_; }
    uint32_t emitMaxCycles() const { return emit_max_cycles_; }
    uint32_t emitTotalCycles() const { return emit_total_cycles_; }
    uint32_t emitCalls()    const { return emit_calls_; }

    static uint32_t cycles();     /* DWT->CYCCNT */
    static uint32_t cyclesToNs(uint32_t c);

private:
    bool pushDma();

    static constexpr uint16_t kRingBytes = 4096;   /* .bss, not the task stack */
    static constexpr uint16_t kLineMax   = 160;

    uint8_t  ring_[kRingBytes] = {};
    volatile uint16_t head_ = 0;      /* producer: emit() */
    volatile uint16_t tail_ = 0;      /* consumer: service()/DMA */
    volatile uint16_t dma_len_ = 0;   /* bytes currently in flight, 0 = idle */
    uint32_t dropped_ = 0;
    uint32_t emit_max_cycles_ = 0;
    uint32_t emit_total_cycles_ = 0;
    uint32_t emit_calls_ = 0;
};

extern Report report;

}  // namespace app::can
