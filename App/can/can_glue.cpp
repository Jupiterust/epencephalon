/*
 * can_glue.cpp - the C linkage between CubeMX's interrupt vectors and the
 * C++ bus objects.
 *
 * stm32f4xx_it.c is generated and stays C, so it cannot name a C++ object.
 * One forwarder per bus, called from inside that file's USER CODE markers so
 * that regenerating from CubeMX keeps it.
 *
 * This library uses no HAL weak callbacks (HAL_CAN_TxMailbox*CompleteCallback
 * and friends), so there is no global weak symbol for two instances to fight
 * over and no per-handle dispatch to write.
 */
#include "can_config.hpp"

extern "C" void app_can1_on_interrupt(void) { app::can::busCan1.onInterrupt(); }

// --- ISR timing -------------------------------------------------------------
//
// Bracketed in stm32f4xx_it.c so the measurement covers HAL_CAN_IRQHandler as
// well as onInterrupt(). Plain volatiles rather than a critical section: the
// four vectors share one priority and cannot pre-empt each other, so there is
// exactly one writer at a time. That is the same structural argument the
// library makes for not having an ISR-side lock.
volatile uint32_t g_isrStartCycles = 0;
volatile uint32_t g_isrMaxCycles = 0;
volatile uint32_t g_isrTotalCycles = 0;
volatile uint32_t g_isrCalls = 0;

extern "C" void app_can1_isr_enter(void) { g_isrStartCycles = DWT->CYCCNT; }

extern "C" void app_can1_isr_exit(void)
{
    const uint32_t d = DWT->CYCCNT - g_isrStartCycles;
    if (d > g_isrMaxCycles) { g_isrMaxCycles = d; }
    g_isrTotalCycles += d;
    ++g_isrCalls;
}
