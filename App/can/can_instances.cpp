#include "can_config.hpp"

#include "can.h"

namespace app::can {

// CAN1 only for now: phase 0 is a single board in loopback, no bus, no second
// node. CAN2 joins in phase 2 and will need its own instance here with
// kCan2Filter (bank 14) and the same slaveStartBank.
jm::master::BxCanBus busCan1(hcan1, jm::master::kCan1Filter, kTiming, kCan1Vectors);

}  // namespace app::can
