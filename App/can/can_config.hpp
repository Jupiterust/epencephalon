/*
 * can_config.hpp - the one place this project configures the CAN master.
 *
 * Everything the library needs that is a property of THIS board lives here:
 * which handles, which vectors, which filter, which bit timing. Nothing is
 * inferred and nothing is duplicated elsewhere.
 *
 * Correctness preconditions this file is responsible for (docs/CAN协议.md
 * section 0, "正确性前提，不满足就静默损坏"):
 *
 *   - All four vectors of one instance carry the SAME NVIC priority, and all
 *     four are enabled. HAL_CAN_IRQHandler does not know which vector entered
 *     it, so the receive path runs from all four; unequal priorities mean two
 *     producers on one receive ring. Configured in CubeMX (can.c:125-132) and
 *     read back by BxCanBus::start().
 *   - That priority is numerically >= configLIBRARY_MAX_SYSCALL_INTERRUPT_
 *     PRIORITY, or taskENTER_CRITICAL()'s BASEPRI mask does not stop them and
 *     the transmit ring's critical section becomes a no-op.
 *   - CAN1's clock is enabled even when only CAN2 is used: the 28 filter
 *     banks are physically CAN1's.
 *
 * The bit timing is the library's own constant, not a number typed here. It
 * carries static_asserts for 1 Mbit/s and an 86.67 % sample point at
 * APB1 = 45 MHz, which is what this project's clock tree produces
 * (SYSCLK 180 MHz / 4).
 */
#pragma once

#include "bxcan_filter.hpp"
#include "can_bus_stm32_bxcan.hpp"

#include "FreeRTOSConfig.h"

namespace app::can {

// The scheduler's syscall threshold, as the library's NVIC check uses it.
// static_assert here as well as the read-back in start(): the constant is the
// intent, the register is the evidence, and only the second one can be wrong
// in a way that matters.
inline constexpr uint32_t kVectorPriority = configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY;
static_assert(kVectorPriority >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY,
              "CAN vectors must sit at or below the scheduler's syscall threshold");

inline constexpr jm::master::BxCanVectors kCan1Vectors{CAN1_TX_IRQn, CAN1_RX0_IRQn,
                                                       CAN1_RX1_IRQn, CAN1_SCE_IRQn};
inline constexpr jm::master::BxCanVectors kCan2Vectors{CAN2_TX_IRQn, CAN2_RX0_IRQn,
                                                       CAN2_RX1_IRQn, CAN2_SCE_IRQn};

// APB1 = 45 MHz on this board. The alternative kTiming42MHz exists for a
// 168 MHz SYSCLK; picking the wrong one is "nothing gets through at all",
// with exactly the symptoms of a misconfigured filter.
inline constexpr auto& kTiming = jm::master::kTiming45MHz;

// Defined in can_instances.cpp. Declared here so the IRQ glue and the
// self-test refer to the same objects; there is no second set anywhere.
extern jm::master::BxCanBus busCan1;

}  // namespace app::can
