#include "selftest.hpp"

#include "can_config.hpp"
#include "report.hpp"

#include "can.h"
#include "can_master.hpp"

#include <new>
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"

#include <cstdio>

/* Defined in can_glue.cpp at global scope. Declared here rather than in a
 * header because they exist only for the bring-up measurement. */
extern volatile uint32_t g_isrMaxCycles;
extern volatile uint32_t g_isrTotalCycles;
extern volatile uint32_t g_isrCalls;

namespace app::can {
namespace {

using jm::master::BxCanMode;
using jm::master::IrqPriorityFault;

SelfTestTotals g_totals;

void record(bool pass, const char *id, const char *name, const char *detail)
{
    if (pass) { ++g_totals.passed; } else { ++g_totals.failed; }
    report.emit("%-5s %-24s %s  %s\r\n", id, name, pass ? "PASS" : "FAIL", detail);
}

const char *faultName(IrqPriorityFault f)
{
    /* Both bits can be set at once; the library reports them together on
     * purpose (irq_priority.hpp:78-86), so a caller that fixes one and stops
     * is not left with the other. */
    const bool unequal = (f & IrqPriorityFault::Unequal) != IrqPriorityFault::None;
    const bool above   = (f & IrqPriorityFault::AboveSyscallThreshold) != IrqPriorityFault::None;
    if (unequal && above) { return "Unequal|AboveSyscallThreshold"; }
    if (unequal)          { return "Unequal"; }
    if (above)            { return "AboveSyscallThreshold"; }
    return "None";
}

// ---------------------------------------------------------------------------
// Test 0: the NVIC rules are checked against the registers, not the constants
// ---------------------------------------------------------------------------
//
// start() reads the four priorities back out of the NVIC and refuses to go on
// the bus if they are unequal or above the scheduler's syscall threshold.
// Those lines have never been executed: the driver is compiled out of the host
// tests (docs/CAN协议.md section 0). So each rule is broken on purpose once and
// the refusal is required, then the correct configuration is required to start.
//
// Enable bits are checked too, not just priorities. Priorities alone would
// pass with SCE masked off, and a disabled SCE vector means no mailbox refill
// on the error path -- silent, and exactly the kind of thing that only shows
// up under load.

void setVectorPriorities(uint32_t tx, uint32_t rx0, uint32_t rx1, uint32_t sce)
{
    HAL_NVIC_SetPriority(kCan1Vectors.tx,  tx,  0);
    HAL_NVIC_SetPriority(kCan1Vectors.rx0, rx0, 0);
    HAL_NVIC_SetPriority(kCan1Vectors.rx1, rx1, 0);
    HAL_NVIC_SetPriority(kCan1Vectors.sce, sce, 0);
}

/* Back to the peripheral state MX_CAN1_Init leaves behind, so each sub-case
 * starts from the same place. HAL_CAN_Init is what puts the controller back
 * into initialisation mode; without the DeInit the second init() would be
 * operating on a started controller. */
bool resetPeripheral()
{
    if (HAL_CAN_DeInit(&hcan1) != HAL_OK) { return false; }
    MX_CAN1_Init();
    return true;
}

bool allFourEnabled()
{
    const IRQn_Type v[4] = {kCan1Vectors.tx, kCan1Vectors.rx0,
                            kCan1Vectors.rx1, kCan1Vectors.sce};
    for (IRQn_Type irq : v) {
        const uint32_t i = static_cast<uint32_t>(irq);
        if ((NVIC->ISER[i >> 5] & (1u << (i & 0x1F))) == 0) { return false; }
    }
    return true;
}

void test0_nvic()
{
    char detail[96];

    // --- 0a: unequal priorities must be refused -----------------------------
    if (!resetPeripheral()) { record(false, "T00a", "nvic_unequal", "DeInit/Init failed"); }
    else {
        setVectorPriorities(kVectorPriority, kVectorPriority,
                            kVectorPriority + 1, kVectorPriority);
        const bool inited  = busCan1.init(BxCanMode::Loopback);
        const bool started = inited && busCan1.start();
        const IrqPriorityFault f = busCan1.irqPriorityFault();
        const bool pass = inited && !started &&
                          (f & IrqPriorityFault::Unequal) != IrqPriorityFault::None;
        snprintf(detail, sizeof(detail), "init=%d started=%d fault=%s",
                 inited, started, faultName(f));
        record(pass, "T00a", "nvic_unequal", detail);
    }

    // --- 0b: above the syscall threshold must be refused ---------------------
    // Numerically lower = higher priority = outside the region taskENTER_
    // CRITICAL()'s BASEPRI mask covers. This is the configuration a latency
    // chaser would reach for, and it turns the transmit ring's critical
    // section into a no-op.
    if (!resetPeripheral()) { record(false, "T00b", "nvic_above_threshold", "DeInit/Init failed"); }
    else {
        const uint32_t unsafe = kVectorPriority - 1;
        setVectorPriorities(unsafe, unsafe, unsafe, unsafe);
        const bool inited  = busCan1.init(BxCanMode::Loopback);
        const bool started = inited && busCan1.start();
        const IrqPriorityFault f = busCan1.irqPriorityFault();
        const bool pass = inited && !started &&
                          (f & IrqPriorityFault::AboveSyscallThreshold) != IrqPriorityFault::None;
        snprintf(detail, sizeof(detail), "init=%d started=%d prio=%lu fault=%s",
                 inited, started, static_cast<unsigned long>(unsafe), faultName(f));
        record(pass, "T00b", "nvic_above_threshold", detail);
    }

    // --- 0c: the correct configuration must start ---------------------------
    if (!resetPeripheral()) { record(false, "T00c", "nvic_correct", "DeInit/Init failed"); }
    else {
        setVectorPriorities(kVectorPriority, kVectorPriority,
                            kVectorPriority, kVectorPriority);
        const bool inited  = busCan1.init(BxCanMode::Loopback);
        const bool started = inited && busCan1.start();
        const IrqPriorityFault f = busCan1.irqPriorityFault();
        const bool enabled = allFourEnabled();
        const bool pass = inited && started && enabled &&
                          f == IrqPriorityFault::None;
        snprintf(detail, sizeof(detail), "init=%d started=%d enabled=%d prio=%lu fault=%s",
                 inited, started, enabled,
                 static_cast<unsigned long>(kVectorPriority), faultName(f));
        record(pass, "T00c", "nvic_correct", detail);
    }
}

// ---------------------------------------------------------------------------
// Test 0.5: the filter registers actually hold what we computed
// ---------------------------------------------------------------------------
//
// The host test proves, by exhaustion over 2048 ids x {data,remote} x
// {std,ext}, which identifiers a given set of register values accepts. What it
// cannot prove is that those values reached the silicon. This reads them back.
//
// Every expectation is derived from kCan1Filter, never a hex literal, so a
// drift in comm::kCmdBits moves the expectation with it. The only fixed
// numbers are the register bit positions, which are RM0090's, not the
// library's.
//
// The filter registers live in CAN1's block for BOTH controllers -- that is
// why CAN1's clock has to be on even when only CAN2 is used -- so this reads
// CAN1 regardless of which instance is under test.

void test05_filter_readback()
{
    const auto &f = jm::master::kCan1Filter;
    const uint32_t bankBit = 1u << f.bank;
    const uint32_t expectId   = jm::master::filterWord(f.idHigh, f.idLow);
    const uint32_t expectMask = jm::master::filterWord(f.maskIdHigh, f.maskIdLow);

    const uint32_t fa1r  = CAN1->FA1R;
    const uint32_t fs1r  = CAN1->FS1R;
    const uint32_t fm1r  = CAN1->FM1R;
    const uint32_t ffa1r = CAN1->FFA1R;
    const uint32_t fmr   = CAN1->FMR;
    const uint32_t fir1  = CAN1->sFilterRegister[f.bank].FR1;
    const uint32_t fir2  = CAN1->sFilterRegister[f.bank].FR2;
    const uint32_t can2sb = (fmr >> CAN_FMR_CAN2SB_Pos) & 0x3Fu;

    char detail[128];

    const bool active = (fa1r & bankBit) != 0;
    snprintf(detail, sizeof(detail), "bank=%u FA1R=0x%08lX", f.bank,
             static_cast<unsigned long>(fa1r));
    record(active, "T05a", "filter_activated", detail);

    /* 32-bit scale: one filter covering the whole identifier word. */
    const bool scale32 = (fs1r & bankBit) != 0;
    snprintf(detail, sizeof(detail), "FS1R=0x%08lX", static_cast<unsigned long>(fs1r));
    record(scale32, "T05b", "filter_scale_32bit", detail);

    /* Mask mode, not list mode: the bit must be CLEAR. */
    const bool maskMode = (fm1r & bankBit) == 0;
    snprintf(detail, sizeof(detail), "FM1R=0x%08lX", static_cast<unsigned long>(fm1r));
    record(maskMode, "T05c", "filter_mask_mode", detail);

    /* FIFO 0: the bit must be CLEAR. The driver drains FIFO 0. */
    const bool fifo0 = (ffa1r & bankBit) == 0;
    snprintf(detail, sizeof(detail), "FFA1R=0x%08lX", static_cast<unsigned long>(ffa1r));
    record(fifo0, "T05d", "filter_fifo0", detail);

    const bool idOk = (fir1 == expectId);
    snprintf(detail, sizeof(detail), "FiR1=0x%08lX want=0x%08lX",
             static_cast<unsigned long>(fir1), static_cast<unsigned long>(expectId));
    record(idOk, "T05e", "filter_id_word", detail);

    const bool maskOk = (fir2 == expectMask);
    snprintf(detail, sizeof(detail), "FiR2=0x%08lX want=0x%08lX",
             static_cast<unsigned long>(fir2), static_cast<unsigned long>(expectMask));
    record(maskOk, "T05f", "filter_mask_word", detail);

    /* One split point, and it has to be the one both instances were given. */
    const bool sbOk = (can2sb == f.slaveStartBank);
    snprintf(detail, sizeof(detail), "CAN2SB=%lu want=%u FMR=0x%08lX",
             static_cast<unsigned long>(can2sb), f.slaveStartBank,
             static_cast<unsigned long>(fmr));
    record(sbOk, "T05g", "filter_slave_start_bank", detail);
}


// ---------------------------------------------------------------------------
// Tests 1-4: does the acceptance filter behave the way the register values say
// ---------------------------------------------------------------------------
//
// Transmission goes through raw HAL_CAN_AddTxMessage, NOT through the
// library's send(). Two reasons:
//
//   - CanFrame's `extended` and `rtr` are never set by this library
//     (can_bus.hpp:11-18), so tests 3 and 4 cannot be expressed through it at
//     all without relying on transport behaviour nobody has verified.
//   - What is under test here is the silicon filter. Routing through the
//     transmit ring would put an unverified dependency between the stimulus
//     and the thing being measured; the ring gets its own tests later.
//
// Reception does go through busCan1.receive(): in loopback the controller
// hands its own frame to the receive FIFO, the interrupt drains it into the
// ring, and that is the path the rest of the system will use.
//
// Tests 2, 3 and 4 are the ones that carry weight. A filter that accepts
// everything passes test 1. Only the three rejections tell it apart from a
// correct one -- so both halves have to pass, and a "pass" on test 1 alone
// means nothing.

/* Empties the receive ring so a previous case cannot be mistaken for this
 * one's result. */
void drainRx()
{
    jm::master::CanFrame f;
    while (busCan1.receive(f)) { }
}

/* One frame straight into a mailbox. Returns false if no mailbox was free. */
bool rawSend(uint32_t id, bool extended, bool remote, uint8_t dlc)
{
    CAN_TxHeaderTypeDef h{};
    if (extended) {
        h.IDE = CAN_ID_EXT;
        h.ExtId = id;
    } else {
        h.IDE = CAN_ID_STD;
        h.StdId = id;
    }
    h.RTR = remote ? CAN_RTR_REMOTE : CAN_RTR_DATA;
    h.DLC = dlc;
    h.TransmitGlobalTime = DISABLE;

    /* Distinct non-zero bytes: a payload of zeros would let a byte-order or
     * offset error through unnoticed. */
    uint8_t data[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint32_t mailbox = 0;
    return HAL_CAN_AddTxMessage(&hcan1, &h, data, &mailbox) == HAL_OK;
}

/* Waits for one frame to come back. In loopback a DLC-8 frame is ~135 us on
 * the wire, so 20 ms is four orders of magnitude of slack; a miss here is a
 * rejection, not a race. */
bool awaitRx(jm::master::CanFrame &out, uint32_t timeoutMs)
{
    const uint32_t start = osKernelGetTickCount();
    while (osKernelGetTickCount() - start < pdMS_TO_TICKS(timeoutMs)) {
        if (busCan1.receive(out)) { return true; }
        osDelay(pdMS_TO_TICKS(1));
    }
    return false;
}

void test1to4_filter_behaviour()
{
    char detail[128];

    /* Every identifier below is built from the firmware's own constants. A
     * hand-written 0x21 or 0xA0 would be blind to a change in kCmdBits, and
     * surviving that change is the whole reason the derivation exists. */
    constexpr uint32_t kHeartbeat = static_cast<uint32_t>(jm::comm::CanCmd::Heartbeat);
    constexpr uint32_t kInsideId  = jm::comm::frameId(1, kHeartbeat);
    constexpr uint32_t kOutsideId =
        jm::comm::frameId(jm::master::kHighestNode + 1u, kHeartbeat);

    /* The filter's own model agrees with what this test expects. If these ever
     * disagree the build stops, rather than the bench reporting a green run
     * against the wrong expectation. */
    static_assert(jm::master::accepts(jm::master::kCan1Filter, kInsideId, false, false),
                  "node 1 data frame must be inside the accepted set");
    static_assert(!jm::master::accepts(jm::master::kCan1Filter, kOutsideId, false, false),
                  "node above kHighestNode must be outside it");
    static_assert(!jm::master::accepts(jm::master::kCan1Filter, kInsideId, false, true),
                  "remote frames must be outside it");
    static_assert(!jm::master::accepts(jm::master::kCan1Filter, kInsideId, true, false),
                  "extended frames must be outside it");

    // --- 1: an in-range node must arrive ------------------------------------
    drainRx();
    jm::master::CanFrame got{};
    bool sent = rawSend(kInsideId, false, false, 8);
    bool recv = sent && awaitRx(got, 20);
    bool idOk = recv && got.id == kInsideId && !got.extended && !got.rtr;
    snprintf(detail, sizeof(detail), "sent=%d recv=%d id=0x%03lX want=0x%03lX dlc=%u",
             sent, recv, static_cast<unsigned long>(got.id),
             static_cast<unsigned long>(kInsideId), got.dlc);
    record(sent && recv && idOk, "T01", "filter_accept", detail);

    // --- 2: a node above kHighestNode must not ------------------------------
    drainRx();
    sent = rawSend(kOutsideId, false, false, 8);
    recv = sent && awaitRx(got, 20);
    snprintf(detail, sizeof(detail), "sent=%d recv=%d id=0x%03lX highestNode=%u",
             sent, recv, static_cast<unsigned long>(kOutsideId),
             jm::master::kCan1Filter.highestNode);
    record(sent && !recv, "T02", "filter_reject_node", detail);

    // --- 3: extended frames must not ---------------------------------------
    // The 29-bit identifier deliberately carries the accepted 11-bit pattern in
    // its low bits: a filter that only compared STID would let it through, and
    // this is what catches that.
    drainRx();
    const uint32_t extId = (0x1234u << 11) | kInsideId;
    sent = rawSend(extId, true, false, 8);
    recv = sent && awaitRx(got, 20);
    snprintf(detail, sizeof(detail), "sent=%d recv=%d extid=0x%08lX",
             sent, recv, static_cast<unsigned long>(extId));
    record(sent && !recv, "T03", "filter_reject_extended", detail);

    // --- 4: remote frames must not -----------------------------------------
    // Same identifier as test 1, so the only difference is RTR. If this one
    // arrives, the RTR bit in the mask (kCan1Filter.maskIdLow) is not doing
    // its job.
    drainRx();
    sent = rawSend(kInsideId, false, true, 0);
    recv = sent && awaitRx(got, 20);
    snprintf(detail, sizeof(detail), "sent=%d recv=%d id=0x%03lX rtr=1",
             sent, recv, static_cast<unsigned long>(kInsideId));
    record(sent && !recv, "T04", "filter_reject_remote", detail);

    /* The bus must be healthy after all of this. In loopback the controller
     * acknowledges its own frames, so any ackError here would mean the mode
     * bits did not take effect. */
    const auto d = busCan1.diagnostics();
    snprintf(detail, sizeof(detail), "rx=%lu ackErr=%lu txErr=%lu ovfFifo=%lu ovfRing=%lu",
             static_cast<unsigned long>(d.framesReceived),
             static_cast<unsigned long>(d.ackError),
             static_cast<unsigned long>(d.txError),
             static_cast<unsigned long>(d.rxFifoOverrun),
             static_cast<unsigned long>(d.rxRingOverflow));
    record(d.ackError == 0 && d.rxFifoOverrun == 0 && d.rxRingOverflow == 0,
           "T04z", "loopback_bus_clean", detail);
}


// ---------------------------------------------------------------------------
// Tests 5-8: the transmit ring, mailbox refill, abort, and receive overflow
// ---------------------------------------------------------------------------
//
// From here on the stimulus goes through the library's own send() -- the ring,
// the critical section, and mailbox loading from the interrupt. docs/CAN协议.md
// section 0 lists all of that as either unverified or structurally unverifiable
// on the host: "LoopbackBus 里没有任何临界区，宿主也没有真中断".
//
// Frame payloads carry the sequence number and its complement, so a frame that
// arrives with the right identifier but the wrong body is still caught. A
// payload of zeros would make an offset error invisible.

jm::master::CanFrame makeFrame(uint32_t node, uint8_t seq)
{
    jm::master::CanFrame f{};
    f.id = jm::comm::frameId(node, static_cast<uint32_t>(jm::comm::CanCmd::Heartbeat));
    f.dlc = 8;
    f.data[0] = seq;
    f.data[1] = static_cast<uint8_t>(~seq);
    f.data[2] = 0x5A;
    f.data[3] = 0xA5;
    f.data[4] = static_cast<uint8_t>(node + 0x10);
    f.data[5] = 0x3C;
    f.data[6] = 0xC3;
    f.data[7] = static_cast<uint8_t>(seq ^ 0xFF);
    return f;
}

/* Collects up to `want` frames, or gives up after timeoutMs. Returns how many
 * arrived; the caller decides whether that is the right number. */
uint32_t collect(jm::master::CanFrame *out, uint32_t want, uint32_t timeoutMs)
{
    uint32_t got = 0;
    const uint32_t start = osKernelGetTickCount();
    while (got < want && osKernelGetTickCount() - start < pdMS_TO_TICKS(timeoutMs)) {
        if (busCan1.receive(out[got])) {
            ++got;
        } else {
            osDelay(pdMS_TO_TICKS(1));
        }
    }
    return got;
}

/* .bss, not the task stack. sizeof(CanFrame) is 16, so rx[80] is 1280 bytes;
 * on the 2560-byte app task stack that overflowed and faulted inside
 * PendSV_Handler (an unaligned STM during context save -- the classic stack
 * overflow signature, and silent because configCHECK_FOR_STACK_OVERFLOW is
 * off). Large test buffers live here from now on. */
jm::master::CanFrame g_rx[80];

void test5to8_rings()
{
    char detail[128];
    jm::master::CanFrame *rx = g_rx;

    // --- 5: more frames than the hardware FIFO holds, none lost -------------
    // FIFO0 is 3 deep. Eight back-to-back frames overrun it unless the
    // interrupt drains into the software ring in time -- the 165 us budget in
    // docs section 0. A loss here shows up as rxFifoOverrun, in silicon.
    drainRx();
    const auto before5 = busCan1.diagnostics();
    constexpr uint32_t kBurst = 8;
    uint32_t accepted = 0;
    for (uint8_t i = 0; i < kBurst; ++i) {
        if (busCan1.send(makeFrame(i % 4u, static_cast<uint8_t>(0x40 + i)))) { ++accepted; }
    }
    uint32_t got = collect(rx, kBurst, 200);
    auto after5 = busCan1.diagnostics();
    bool bodiesOk = true;
    for (uint32_t i = 0; i < got; ++i) {
        if (rx[i].dlc != 8 ||
            rx[i].data[1] != static_cast<uint8_t>(~rx[i].data[0]) ||
            rx[i].data[7] != static_cast<uint8_t>(rx[i].data[0] ^ 0xFF)) {
            bodiesOk = false;
        }
    }
    snprintf(detail, sizeof(detail), "sent=%lu recv=%lu bodies=%d fifoOvf=%lu ringOvf=%lu",
             static_cast<unsigned long>(accepted), static_cast<unsigned long>(got), bodiesOk,
             static_cast<unsigned long>(after5.rxFifoOverrun - before5.rxFifoOverrun),
             static_cast<unsigned long>(after5.rxRingOverflow - before5.rxRingOverflow));
    record(accepted == kBurst && got == kBurst && bodiesOk &&
           after5.rxFifoOverrun == before5.rxFifoOverrun, "T05", "rx_ring_no_loss", detail);

    // --- 6a: three mailboxes, eight frames: every one gets a mailbox --------
    // The mailbox is loaded only by the interrupt. If refill after a completed
    // frame does not happen, this stalls at three.
    drainRx();
    accepted = 0;
    for (uint8_t i = 0; i < kBurst; ++i) {
        if (busCan1.send(makeFrame(2, static_cast<uint8_t>(0x80 + i)))) { ++accepted; }
    }
    got = collect(rx, kBurst, 200);
    const auto d6 = busCan1.diagnostics();
    snprintf(detail, sizeof(detail), "sent=%lu recv=%lu txRingOvf=%lu txErr=%lu",
             static_cast<unsigned long>(accepted), static_cast<unsigned long>(got),
             static_cast<unsigned long>(d6.txRingOverflow),
             static_cast<unsigned long>(d6.txError));
    record(accepted == kBurst && got == kBurst, "T06a", "tx_mailbox_refill", detail);

    // --- 6b: arbitration order among frames pending at the same time -------
    // TXFP = 0, so the lowest identifier wins -- but only among frames that
    // are pending together. The first frame queued onto an idle bus starts
    // transmitting before the second one is even enqueued (send() raises the
    // TX vector, and the ISR pre-empts this task), so it can never lose
    // arbitration no matter what its identifier is.
    //
    // An earlier version of this test queued high->low and expected low->high
    // back. It got 0x061,0x021,0x041: the first frame was already on the wire,
    // and the remaining two arbitrated correctly. That was a wrong expectation,
    // not a wrong controller.
    //
    // So: one blocker frame occupies the wire, then high and low are queued
    // while it is busy. Only those two are ranked, and low must come first.
    drainRx();
    const uint32_t idHi = jm::comm::frameId(3, static_cast<uint32_t>(jm::comm::CanCmd::Heartbeat));
    const uint32_t idLo = jm::comm::frameId(1, static_cast<uint32_t>(jm::comm::CanCmd::Heartbeat));
    const uint32_t idBlock = jm::comm::frameId(2, static_cast<uint32_t>(jm::comm::CanCmd::Heartbeat));
    busCan1.send(makeFrame(2, 0xC0));           /* blocker: occupies the wire */
    busCan1.send(makeFrame(3, 0xC1));           /* queued high first ... */
    busCan1.send(makeFrame(1, 0xC2));           /* ... low second */
    got = collect(rx, 3, 200);
    /* The blocker goes first because it was already transmitting; the ranking
     * claim is only about the two that waited together. */
    const bool blockerFirst = got == 3 && rx[0].id == idBlock;
    const bool lowBeatsHigh = got == 3 && rx[1].id == idLo && rx[2].id == idHi;
    snprintf(detail, sizeof(detail), "recv=%lu got=0x%03lX,0x%03lX,0x%03lX blocker=0x%03lX lo=0x%03lX hi=0x%03lX",
             static_cast<unsigned long>(got),
             static_cast<unsigned long>(got > 0 ? rx[0].id : 0),
             static_cast<unsigned long>(got > 1 ? rx[1].id : 0),
             static_cast<unsigned long>(got > 2 ? rx[2].id : 0),
             static_cast<unsigned long>(idBlock), static_cast<unsigned long>(idLo),
             static_cast<unsigned long>(idHi));
    record(blockerFirst && lowBeatsHigh, "T06b", "tx_arbitration_order", detail);

    // --- 7a: cancel() while the frame is still in the ring ------------------
    // Five frames: three take mailboxes, two wait in the ring. The fifth is
    // withdrawn, so cancel() takes the ring path -- it shifts the entry out
    // under the critical section and never touches a mailbox.
    //
    // KNOWN UPSTREAM GAP, kept red on purpose. can_bus.hpp:33-39 states the
    // rule "every control frame that does not reach the wire lands on a
    // counter", and README promise 3 repeats it. But cancel()'s ring path
    // (can_bus_stm32_bxcan.cpp:287-297) only sets `withdrawn` -- it increments
    // nothing, while the mailbox path at :303 increments txAborted. A request
    // that times out while still queued therefore disappears from every
    // counter. That is the load case: the busier the ring, the likelier this
    // path, so the evidence goes blind exactly when it is needed.
    drainRx();
    const auto before7a = busCan1.diagnostics();
    for (uint8_t i = 0; i < 4; ++i) { busCan1.send(makeFrame(1, static_cast<uint8_t>(0xD0 + i))); }
    const uint32_t ringVictim = jm::comm::frameId(3, static_cast<uint32_t>(jm::comm::CanCmd::Estop));
    jm::master::CanFrame v1 = makeFrame(3, 0xD8);
    v1.id = ringVictim;
    const bool queued1 = busCan1.send(v1);
    const bool withdrawn1 = busCan1.cancel(ringVictim);
    got = collect(rx, 5, 200);
    const auto after7a = busCan1.diagnostics();
    uint32_t seen1 = 0;
    for (uint32_t i = 0; i < got; ++i) { if (rx[i].id == ringVictim) { ++seen1; } }
    const uint32_t counted1 = (after7a.txAborted - before7a.txAborted) +
                              (after7a.txSuperseded - before7a.txSuperseded) +
                              (after7a.txRingOverflow - before7a.txRingOverflow);
    /* Contract: absent from the wire implies present on some counter. */
    const bool accounted1 = (seen1 > 0) || (counted1 > 0);
    snprintf(detail, sizeof(detail), "queued=%d withdrawn=%d recv=%lu seen=%lu counters=%lu",
             queued1, withdrawn1, static_cast<unsigned long>(got),
             static_cast<unsigned long>(seen1), static_cast<unsigned long>(counted1));
    record(queued1 && withdrawn1 && accounted1, "T07a", "abort_ring_counted", detail);

    // --- 7b: cancel() while the frame is in a mailbox -----------------------
    // Three frames with distinct identifiers: all three reach mailboxes, the
    // first is on the wire and the other two wait there. Cancelling the third
    // takes the mailbox path, which does increment txAborted.
    //
    // Best effort by contract: if it had already started transmitting it comes
    // back, and that is a pass too. What must never happen is vanishing with
    // no counter -- that is the assertion.
    drainRx();
    const auto before7b = busCan1.diagnostics();
    const uint32_t mbVictim = jm::comm::frameId(3, static_cast<uint32_t>(jm::comm::CanCmd::GetMotorError));
    busCan1.send(makeFrame(1, 0xE0));
    busCan1.send(makeFrame(1, 0xE1));
    jm::master::CanFrame v2 = makeFrame(3, 0xE2);
    v2.id = mbVictim;
    const bool queued2 = busCan1.send(v2);
    const bool withdrawn2 = busCan1.cancel(mbVictim);
    got = collect(rx, 3, 200);
    const auto after7b = busCan1.diagnostics();
    uint32_t seen2 = 0;
    for (uint32_t i = 0; i < got; ++i) { if (rx[i].id == mbVictim) { ++seen2; } }
    const uint32_t aborted2 = after7b.txAborted - before7b.txAborted;
    const bool accounted2 = (seen2 > 0) || (aborted2 > 0);
    snprintf(detail, sizeof(detail), "queued=%d withdrawn=%d recv=%lu seen=%lu txAborted=%lu",
             queued2, withdrawn2, static_cast<unsigned long>(got),
             static_cast<unsigned long>(seen2), static_cast<unsigned long>(aborted2));
    record(queued2 && accounted2, "T07b", "abort_mailbox_counted", detail);

    // --- 8: receive ring overflow is counted, not silent --------------------
    // The ring is 64 frames. Nothing drains it here, so past 64 the driver has
    // to start counting. Sent in batches because the transmit ring is 32: a
    // txRingOverflow would mean this test never reached the receive side and
    // is measuring the wrong thing, so it is asserted separately.
    drainRx();
    const auto before8 = busCan1.diagnostics();
    constexpr uint32_t kFlood = 80;              /* > kRxRingSize (64) */
    uint32_t pushed = 0;
    for (uint32_t batch = 0; batch < 5; ++batch) {
        for (uint32_t i = 0; i < 16; ++i) {
            if (busCan1.send(makeFrame(1, static_cast<uint8_t>(batch * 16 + i)))) { ++pushed; }
        }
        osDelay(pdMS_TO_TICKS(5));               /* let the wire drain, do NOT drain rx */
    }
    osDelay(pdMS_TO_TICKS(50));
    const auto after8 = busCan1.diagnostics();
    const uint32_t ringOvf = after8.rxRingOverflow - before8.rxRingOverflow;
    const uint32_t txOvf = after8.txRingOverflow - before8.txRingOverflow;
    snprintf(detail, sizeof(detail), "pushed=%lu of %lu rxRingOvf=%lu txRingOvf=%lu fifoOvf=%lu",
             static_cast<unsigned long>(pushed), static_cast<unsigned long>(kFlood),
             static_cast<unsigned long>(ringOvf), static_cast<unsigned long>(txOvf),
             static_cast<unsigned long>(after8.rxFifoOverrun - before8.rxFifoOverrun));
    record(pushed == kFlood && txOvf == 0 && ringOvf > 0, "T08", "rx_ring_overflow_counted", detail);

    drainRx();
}



/* CanMaster takes the bus by reference and has no default constructor, so it
 * cannot simply be a namespace-scope object initialised before busCan1. This
 * holds it in .bss and constructs it in place on first use, after the bus has
 * been started. */
class CanMasterHolder
{
public:
    jm::master::CanMaster &master()
    {
        if (m_master == nullptr) {
            m_master = new (m_storage) jm::master::CanMaster(busCan1);
        }
        return *m_master;
    }

private:
    alignas(jm::master::CanMaster) unsigned char m_storage[sizeof(jm::master::CanMaster)];
    jm::master::CanMaster *m_master = nullptr;
};

// ---------------------------------------------------------------------------
// Tests 9-10: the time-based state, on the real clock
// ---------------------------------------------------------------------------
//
// Both run against the real BxCanBus, whose nowMs() is HAL_GetTick(). The
// library's BxCanBus is `final` (can_bus_stm32_bxcan.hpp:343), so the clock
// cannot be stubbed by deriving from it -- and that turns out to be the more
// honest test anyway: the stall detector is driven by making poll() run faster
// than the tick, which is the real clock actually repeating itself.

CanMasterHolder g_holder;   /* CanMaster has no default ctor; see below */

void test9to10_time()
{
    char detail[160];

    // --- 9: a stalled clock makes every node offline ------------------------
    // CanMaster::poll() (can_master.cpp:352-364) counts consecutive polls that
    // read the same millisecond; at kClockStallPolls (500) it latches
    // m_clockStalled, and isOnline() then returns false for everything
    // regardless of when the node was last heard (:378-381).
    //
    // Nothing is stubbed: poll() is simply called in a tight loop, so
    // HAL_GetTick() really does return the same value many times running. In
    // production poll() runs every 3 ms and can never see this.
    auto &master = g_holder.master();
    jm::master::JointHandle *j1 = master.addJoint(1);

    /* Make the node online first, so the transition being observed is the
     * clock's doing and not "it was never seen". In loopback a frame we send
     * comes straight back, and request and reply share one identifier
     * (README:546), so the master books it as traffic from that node. */
    drainRx();
    busCan1.send(makeFrame(1, 0x90));
    for (int i = 0; i < 50; ++i) { master.poll(); osDelay(pdMS_TO_TICKS(1)); }
    const bool onlineBefore = master.isOnline(1);

    /* Stop the clock for real. HAL's time base is TIM6 on this project
     * (Core/Src/stm32f4xx_hal_timebase_tim.c), separate from the SysTick that
     * drives FreeRTOS, so suspending it freezes HAL_GetTick() -- which is what
     * BxCanBus::nowMs() returns -- while the scheduler keeps running.
     *
     * An earlier attempt just span poll() in a tight loop hoping to read the
     * same millisecond 500 times running. It cannot work here: poll() measures
     * 3.57 us, so only ~280 fit in a tick and the counter resets every time
     * the tick moves. 20000 polls spanned 51 ms and never latched. That was a
     * wrong method, not a missing feature.
     *
     * Nothing between suspend and resume may wait on a HAL timeout. */
    HAL_SuspendTick();
    const uint32_t frozen = HAL_GetTick();
    uint32_t polls = 0;
    while (!master.clockStalled() && polls < 4000) {
        master.poll();
        ++polls;
    }
    const bool stalled = master.clockStalled();
    const bool offlineWhileStalled = !master.isOnline(1);
    const bool reallyFrozen = (HAL_GetTick() == frozen);
    HAL_ResumeTick();

    snprintf(detail, sizeof(detail),
             "onlineBefore=%d stalled=%d polls=%lu frozen=%d threshold=%lu offline=%d",
             onlineBefore, stalled, static_cast<unsigned long>(polls), reallyFrozen,
             static_cast<unsigned long>(jm::master::CanMaster::kClockStallPolls),
             offlineWhileStalled);
    record(onlineBefore && stalled && reallyFrozen && offlineWhileStalled,
           "T09", "clock_stall_offline", detail);

    /* And it has to clear again once the tick moves, or a single fast loop
     * would brick the online state for good. */
    osDelay(pdMS_TO_TICKS(5));
    master.poll();
    const bool cleared = !master.clockStalled();
    snprintf(detail, sizeof(detail), "clockStalled=%d online=%d",
             master.clockStalled(), master.isOnline(1));
    record(cleared, "T09b", "clock_stall_clears", detail);

    // --- 10: silence makes one node offline, on schedule ---------------------
    // offlineAfterMs is shortened from the 300 ms default so the self-test does
    // not sit idle for a third of a second; the threshold under test is the
    // configured one either way, and it is read back into the report rather
    // than written as a literal.
    constexpr uint32_t kOfflineMs = 60;
    master.setOfflineAfterMs(kOfflineMs);

    drainRx();
    busCan1.send(makeFrame(1, 0x91));
    for (int i = 0; i < 30; ++i) { master.poll(); osDelay(pdMS_TO_TICKS(1)); }
    const bool onlineAfterTraffic = master.isOnline(1);

    /* Measured from the library's own record of when the node was last heard,
     * not from when this test stopped sending. Those are not the same instant:
     * the frame arrives at the start of the polling loop above, so timing from
     * the end of it reported 32 ms against a 60 ms threshold -- the reference
     * point was wrong, not the detector. */
    const uint32_t seenAt = master.lastSeenMs(1);
    uint32_t flippedAt = 0;
    for (int i = 0; i < 400 && flippedAt == 0; ++i) {
        master.poll();
        if (!master.isOnline(1)) { flippedAt = HAL_GetTick() - seenAt; }
        osDelay(pdMS_TO_TICKS(1));
    }
    /* At the threshold, and not far past it: the margin covers the 1 ms poll
     * granularity plus scheduler jitter. */
    const bool inWindow = flippedAt > kOfflineMs && flippedAt <= kOfflineMs + 15;
    snprintf(detail, sizeof(detail), "onlineAfterTraffic=%d flippedAtMs=%lu threshold=%lu seenAt=%lu",
             onlineAfterTraffic, static_cast<unsigned long>(flippedAt),
             static_cast<unsigned long>(kOfflineMs), static_cast<unsigned long>(seenAt));
    record(onlineAfterTraffic && flippedAt != 0 && inWindow, "T10", "offline_detection", detail);

    master.setOfflineAfterMs(jm::master::CanMaster::kDefaultOfflineMs);
    (void)j1;
}

// ---------------------------------------------------------------------------
// The two numbers the timing budget needs
// ---------------------------------------------------------------------------
//
// The 165 us drain budget in docs section 0 is for the receive ISR; poll()'s
// cost and jitter feed the 3 ms control period. Both measured with DWT, both
// with the report's own cost already characterised separately so it can be
// subtracted.

void testPerf()
{
    char detail[160];
    auto &master = g_holder.master();

    /* poll() over a burst of real traffic, so the measurement covers the
     * dispatch path and not just an empty ring. */
    uint32_t pollMin = 0xFFFFFFFFu, pollMax = 0, pollSum = 0;
    constexpr uint32_t kIters = 200;
    for (uint32_t i = 0; i < kIters; ++i) {
        if ((i % 8) == 0) { busCan1.send(makeFrame(1, static_cast<uint8_t>(i))); }
        const uint32_t t0 = Report::cycles();
        master.poll();
        const uint32_t d = Report::cycles() - t0;
        if (d < pollMin) { pollMin = d; }
        if (d > pollMax) { pollMax = d; }
        pollSum += d;
        osDelay(pdMS_TO_TICKS(1));
    }
    snprintf(detail, sizeof(detail), "min=%luc/%luns max=%luc/%luns avg=%luc/%luns n=%lu",
             static_cast<unsigned long>(pollMin),
             static_cast<unsigned long>(Report::cyclesToNs(pollMin)),
             static_cast<unsigned long>(pollMax),
             static_cast<unsigned long>(Report::cyclesToNs(pollMax)),
             static_cast<unsigned long>(pollSum / kIters),
             static_cast<unsigned long>(Report::cyclesToNs(pollSum / kIters)),
             static_cast<unsigned long>(kIters));
    report.emit("PERF poll %s\r\n", detail);

    /* The ISR is timed inside stm32f4xx_it.c, where the whole handler can be
     * bracketed -- HAL_CAN_IRQHandler included, which is most of it. */
    const uint32_t calls = ::g_isrCalls;
    report.emit("PERF can_isr max=%luc/%luns avg=%luc/%luns n=%lu budget=165000ns\r\n",
                static_cast<unsigned long>(::g_isrMaxCycles),
                static_cast<unsigned long>(Report::cyclesToNs(::g_isrMaxCycles)),
                static_cast<unsigned long>(calls ? ::g_isrTotalCycles / calls : 0),
                static_cast<unsigned long>(Report::cyclesToNs(calls ? ::g_isrTotalCycles / calls : 0)),
                static_cast<unsigned long>(calls));

    const bool withinBudget = Report::cyclesToNs(::g_isrMaxCycles) < 165000u;
    snprintf(detail, sizeof(detail), "maxNs=%lu budgetNs=165000",
             static_cast<unsigned long>(Report::cyclesToNs(::g_isrMaxCycles)));
    record(calls > 0 && withinBudget, "T11", "isr_within_drain_budget", detail);
}

}  // namespace

SelfTestTotals runSelfTest()
{
    g_totals = SelfTestTotals{};

    report.emit("SELFTEST BEGIN port=FREERTOS timing=%luHz/%lu/%lu/%lu/%lu prio=%lu\r\n",
                static_cast<unsigned long>(kTiming.apb1Hz),
                static_cast<unsigned long>(kTiming.prescaler),
                static_cast<unsigned long>(kTiming.bs1),
                static_cast<unsigned long>(kTiming.bs2),
                static_cast<unsigned long>(kTiming.sjw),
                static_cast<unsigned long>(kVectorPriority));
    report.emit("SELFTEST bitrate=%lu samplepoint=%lu.%02lu%%\r\n",
                static_cast<unsigned long>(kTiming.bitrate()),
                static_cast<unsigned long>(kTiming.samplePointHundredths() / 100),
                static_cast<unsigned long>(kTiming.samplePointHundredths() % 100));

    test0_nvic();
    test05_filter_readback();
    test1to4_filter_behaviour();
    test5to8_rings();
    test9to10_time();
    testPerf();

    report.emit("PERF stack_free_words=%lu frame_bytes=%lu\r\n",
                static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)),
                static_cast<unsigned long>(sizeof(jm::master::CanFrame)));
    report.emit("PERF report_cycles max=%lu avg=%lu calls=%lu dropped=%lu\r\n",
                static_cast<unsigned long>(report.emitMaxCycles()),
                static_cast<unsigned long>(report.emitCalls() ? report.emitTotalCycles() /
                                                                report.emitCalls() : 0),
                static_cast<unsigned long>(report.emitCalls()),
                static_cast<unsigned long>(report.dropped()));
    report.emit("SELFTEST END pass=%u fail=%u\r\n", g_totals.passed, g_totals.failed);
    return g_totals;
}

}  // namespace app::can
