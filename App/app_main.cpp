#include "app_main.h"

#include "led.hpp"
#include "key.hpp"
#include "oled.hpp"
#include "tft.hpp"
#include "sdcard.hpp"

#include "can/report.hpp"
#include "can/selftest.hpp"
#include "can/soak.hpp"
#include "spi_bus.hpp"

#include "cmsis_os.h"

namespace {

constexpr uint32_t kTickMs = 10;    /* 主循环周期，同时也是按键采样周期 */

/* false = 1.3 寸 SH1106 单色屏（当前实际接的这块）
 * true  = 1.8 寸 ILI9163 彩屏（手上那块点不亮，换屏后再试）
 * 两个驱动共用 SpiBus，同一时间只能启用一个。 */
constexpr bool kUseTft = true;

/* 开机自检时长（ms）。0 = 关闭。屏幕确认正常后可以改成 0。 */
constexpr uint32_t kLampTestMs = 0;

/* Soak durations. 0 disables the run entirely.
 * Validation values: 30 s + 90 s (long enough for the rollover, which lands
 * 65.5 s into the wrap phase). Production values per the plan: 30 min normal,
 * 11 min wrap (65 s to reach the rollover plus 10 min past it). */
/* Phase 0's soak has passed (820k cycles, 1.64M frames, all six counters at
 * zero, rollover crossed). Left at 0 so a normal boot is not a 41-minute run;
 * restore 30/11 minutes to repeat it. */
constexpr uint32_t kSoakNormalMs = 0;
constexpr uint32_t kSoakWrapMs   = 0;

struct BootStatus {
    bool     disp_ok    = false;
    bool     sd_present = false;
    bool     sd_mounted = false;
    uint32_t sd_total_mb = 0;
    uint32_t sd_free_mb  = 0;
    uint32_t log_before  = 0;
    uint32_t log_bytes   = 0;
    bool     log_ok      = false;
};

/* 放在静态存储而非任务栈：调试器可直接读，排外设问题时不用加打印 */
BootStatus g_status;
app::can::SelfTestTotals g_can;

BootStatus bringup()
{
    BootStatus st;

    drv::led.init();
    drv::key.init();

    st.disp_ok = kUseTft ? drv::tft.init(false) : drv::oled.init();

    st.sd_present = drv::SdCard::detected();
    if (st.sd_present)
    {
        st.sd_mounted = drv::sdcard.mount();
        if (st.sd_mounted)
            drv::sdcard.capacity(&st.sd_total_mb, &st.sd_free_mb);
    }
    return st;
}

/* ---- SH1106：21 列 x 5 行，先画到 GRAM，最后统一 refresh ---- */
void draw_oled(const BootStatus &st, uint8_t led_mask, bool key_down,
               uint32_t uptime_s)
{
    drv::oled.printf(0, 0, "epencephalon");

    if (!st.sd_present)
        drv::oled.printf(1, 0, "SD : no card");
    else if (!st.sd_mounted)
    {
        const int e = drv::sdcard.last_error();
        if (e == -2)      drv::oled.printf(1, 0, "SD : no DMA cfg");
        else if (e == -3) drv::oled.printf(1, 0, "SD : init fail");
        else              drv::oled.printf(1, 0, "SD : fr %d", e);
    }
    else
        drv::oled.printf(1, 0, "SD : %luM free",
                         static_cast<unsigned long>(st.sd_free_mb));

    drv::oled.printf(2, 0, "LED: %02X  KEY:%s", led_mask, key_down ? "DN" : "UP");
    drv::oled.printf(3, 0, "log: %lu B",
                     static_cast<unsigned long>(st.log_bytes));
    drv::oled.printf(4, 0, "up : %lu s", static_cast<unsigned long>(uptime_s));

    drv::oled.refresh();    /* 只推变化的页；画面不动时零 SPI 流量 */
}

/* ---- ILI9163：没有帧缓冲，直接画，所以分静态/动态两部分 ---- */
using namespace drv::color;

void tft_draw_frame(const BootStatus &st)
{
    drv::tft.clear(kBlack);
    drv::tft.str8x16(0, 0, "epencephalon", kCyan, kBlack);
    drv::tft.draw_hline(0, 17, drv::tft.width(), kGray);

    if (!st.sd_mounted)
        drv::tft.str6x8(0, 3, "SD  not ready", kRed, kBlack);
    else
        drv::tft.printf6x8(0, 3, kGreen, kBlack, "SD  %lu/%lu MB",
                           static_cast<unsigned long>(st.sd_free_mb),
                           static_cast<unsigned long>(st.sd_total_mb));
}

void tft_draw_live(uint8_t led_mask, bool key_down, uint32_t uptime_s)
{
    drv::tft.printf6x8(0, 6, kYellow, kBlack, "LED %02X   KEY %s",
                       led_mask, key_down ? "DOWN" : "UP  ");
    drv::tft.printf6x8(0, 7, kWhite, kBlack, "up %lus  CAN %u/%u",
                       static_cast<unsigned long>(uptime_s),
                       g_can.passed, static_cast<unsigned>(g_can.passed + g_can.failed));
    for (uint8_t i = 0; i < 8; ++i)
        drv::tft.fill_rect(static_cast<uint8_t>(4 + i * 14), 74, 10, 10,
                           (led_mask & (1u << i)) ? kOrange : 0x2104);
}

/* 4096 since the CAN self-test runs here. The previous 2560 faulted inside
 * PendSV during context save -- stack overflow, unreported because
 * configCHECK_FOR_STACK_OVERFLOW is off in FreeRTOSConfig.h. The self-test now
 * prints uxTaskGetStackHighWaterMark so this number follows a measurement. */
constexpr uint32_t kAppStackBytes = 4096;

void app_task(void *)
{
    const BootStatus st = bringup();
    g_status = st;

    if (kLampTestMs != 0 && st.disp_ok)
    {
        if (kUseTft) drv::tft.lamp_test(kLampTestMs / 3);
        else         drv::oled.lamp_test(kLampTestMs);
    }

    /* 开机跑一遍马灯，从 A 到 H，肉眼确认顺序和极性 */
    for (uint8_t i = 0; i < drv::Led::kCount; ++i)
    {
        drv::led.write_mask(static_cast<uint8_t>(1u << i));
        osDelay(pdMS_TO_TICKS(60));
    }
    drv::led.write_mask(0);

    if (st.sd_mounted)
    {
        /* 先记下追加前的大小，和追加后对比即可证明确实落盘了 */
        drv::sdcard.file_size("boot.log", &g_status.log_before);
        g_status.log_ok = drv::sdcard.append_line("boot.log", "epencephalon boot ok");
        drv::sdcard.file_size("boot.log", &g_status.log_bytes);
    }

    if (kUseTft && st.disp_ok)
        tft_draw_frame(g_status);

    /* Phase 0 CAN bring-up. Runs to completion and reports; nothing else on
     * this board talks to CAN yet, so it owns the peripheral while it runs.
     * The result is kept so the display can show it without re-running. */
    if (app::can::report.init())
    {
        g_can = app::can::runSelfTest();
        app::can::report.drain(2000);

        /* Phase 7: the sustained run. Short values first -- the harness gets
         * validated before 40 minutes are committed to it. */
        if (kSoakNormalMs != 0)
        {
            app::can::runSoak(kSoakNormalMs, kSoakWrapMs);
            app::can::report.drain(3000);
        }
    }

    uint32_t ticks = 0;
    uint8_t  led_mask = 0;
    uint32_t last_wake = osKernelGetTickCount();

    for (;;)
    {
        drv::key.poll();

        if (drv::key.take_click())
        {
            led_mask = static_cast<uint8_t>(led_mask ? (led_mask << 1) : 1u);
            drv::led.write_mask(led_mask);
        }
        if (drv::key.take_long_press())
        {
            led_mask = 0;
            drv::led.write_mask(0);
        }

        /* 单色屏 10Hz（脏页比对后实际流量很小）；彩屏 5Hz（每次都真发像素） */
        const uint32_t period = kUseTft ? 200 : 100;
        if (st.disp_ok && ticks % (period / kTickMs) == 0)
        {
            const uint32_t up = ticks * kTickMs / 1000;
            if (kUseTft) tft_draw_live(led_mask, drv::key.pressed(), up);
            else         draw_oled(g_status, led_mask, drv::key.pressed(), up);
        }

        ++ticks;
        last_wake += pdMS_TO_TICKS(kTickMs);
        osDelayUntil(last_wake);
    }
}

} // namespace

void app_main(void)
{
    static const osThreadAttr_t kAttr = {
        .name       = "app",
        .attr_bits  = 0,
        .cb_mem     = nullptr,
        .cb_size    = 0,
        .stack_mem  = nullptr,
        .stack_size = kAppStackBytes,
        .priority   = osPriorityNormal,
        .tz_module  = 0,
        .reserved   = 0,
    };

    if (osThreadNew(app_task, nullptr, &kAttr) == nullptr)
    {
        drv::led.init();
        for (;;)
        {
            drv::led.all(true);  osDelay(pdMS_TO_TICKS(100));
            drv::led.all(false); osDelay(pdMS_TO_TICKS(100));
        }
    }

    osThreadExit();
}
