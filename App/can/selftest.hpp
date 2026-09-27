/*
 * selftest.hpp - phase 0 bring-up self-test.
 *
 * Runs to completion and prints one machine-readable line per case. No menu:
 * every configuration change means re-running it, and two runs have to be
 * diffable.
 *
 * Nothing here is interactive and nothing depends on a second node: phase 0 is
 * one board, no bus, no transceiver, no termination.
 */
#pragma once

#include <cstdint>

namespace app::can {

struct SelfTestTotals {
    uint16_t passed = 0;
    uint16_t failed = 0;
};

/* Tests 0 and 0.5. Later phases append to this. */
SelfTestTotals runSelfTest();

}  // namespace app::can
