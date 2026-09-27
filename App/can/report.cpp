#include "report.hpp"

#include "main.h"
#include "usart.h"
#include "cmsis_os.h"

#include <cstdio>
#include <cstring>

namespace app::can {

Report report;

namespace {
constexpr uint32_t kBaud = 921600;   /* same as the joint driver debug UART */
}

uint32_t Report::cycles() { return DWT->CYCCNT; }

uint32_t Report::cyclesToNs(uint32_t c)
{
    /* SystemCoreClock is 180 MHz here. Integer math only: no double anywhere
     * in this project's hot paths, and this one is called from report paths
     * that the soak test measures. */
    return static_cast<uint32_t>((static_cast<uint64_t>(c) * 1000ull) /
                                 (SystemCoreClock / 1000000ull));
}

bool Report::init()
{
    /* DWT cycle counter: the timing instrument for every PERF line. Needs the
     * trace unit powered before CYCCNT counts at all. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    /* No DWT->LAR here: the lock register is Cortex-M7. On the M4 the trace
     * unit is unlocked once DEMCR.TRCENA is set. */
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0) {
        return false;                   /* no counter, no measurements */
    }

    /* CubeMX generates 115200 (usart.c:42). Overridden here so the baud is a
     * property of this module rather than of a regeneration cycle. */
    if (huart7.Init.BaudRate != kBaud) {
        huart7.Init.BaudRate = kBaud;
        if (HAL_UART_Init(&huart7) != HAL_OK) {
            return false;
        }
    }

    head_ = tail_ = dma_len_ = 0;
    dropped_ = 0;
    emit_max_cycles_ = emit_total_cycles_ = emit_calls_ = 0;
    return true;
}

bool Report::emit(const char *fmt, ...)
{
    const uint32_t t0 = cycles();

    char line[kLineMax];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) {
        ++dropped_;
        return false;
    }
    if (n > static_cast<int>(sizeof(line) - 1)) {
        n = sizeof(line) - 1;           /* truncated, still emitted */
    }

    /* Free space, leaving one byte so head == tail always means empty. */
    const uint16_t head = head_;
    const uint16_t tail = tail_;
    const uint16_t used = static_cast<uint16_t>((head - tail) & (kRingBytes - 1));
    if (static_cast<uint16_t>(used + n) >= kRingBytes) {
        ++dropped_;                     /* counted, never silent */
        return false;
    }

    for (int i = 0; i < n; ++i) {
        ring_[(head + i) & (kRingBytes - 1)] = static_cast<uint8_t>(line[i]);
    }
    head_ = static_cast<uint16_t>((head + n) & (kRingBytes - 1));

    const uint32_t spent = cycles() - t0;
    if (spent > emit_max_cycles_) {
        emit_max_cycles_ = spent;
    }
    emit_total_cycles_ += spent;
    ++emit_calls_;
    return true;
}

bool Report::pushDma()
{
    if (dma_len_ != 0) {
        return false;                   /* still in flight */
    }
    const uint16_t head = head_;
    const uint16_t tail = tail_;
    if (head == tail) {
        return false;                   /* empty */
    }
    /* One contiguous span; a wrap is simply sent as two transfers. */
    const uint16_t span = (head > tail) ? static_cast<uint16_t>(head - tail)
                                        : static_cast<uint16_t>(kRingBytes - tail);
    if (HAL_UART_Transmit_DMA(&huart7, &ring_[tail], span) != HAL_OK) {
        return false;
    }
    dma_len_ = span;
    return true;
}

void Report::service()
{
    if (dma_len_ != 0 && huart7.gState == HAL_UART_STATE_READY) {
        tail_ = static_cast<uint16_t>((tail_ + dma_len_) & (kRingBytes - 1));
        dma_len_ = 0;
    }
    pushDma();
}

void Report::drain(uint32_t timeout_ms)
{
    const uint32_t start = osKernelGetTickCount();
    while (osKernelGetTickCount() - start < pdMS_TO_TICKS(timeout_ms)) {
        service();
        if (dma_len_ == 0 && head_ == tail_) {
            return;
        }
        osDelay(pdMS_TO_TICKS(1));
    }
}

}  // namespace app::can
