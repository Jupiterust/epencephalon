/*
 * spi_bus.hpp — 显示屏共用的 SPI1 传输层（SPI1 + DMA2_Stream5）
 *
 * 一块板上同时留着两个屏驱动（SH1106 单色 / ILI9163 彩屏），但它们共用
 * 同一条 SPI 和同一组控制线，也只能有一个 HAL_SPI_TxCpltCallback，
 * 所以传输层抽到这里，驱动层只管协议。
 *
 * 接线（板上 OLED 排针）：
 *   PB3  SPI1_SCK   -> SCL / S0
 *   PA7  SPI1_MOSI  -> SDA / D1
 *   PB9  OLED_DC    -> DC   (0=命令 1=数据)
 *   PB10 OLED_RST   -> RST  (低复位)
 *   PA6  TFT_CS     -> CS   (低有效)
 *
 * 两条硬件结论（实机验证，别动）：
 *  - DC 必须在它所标注的整个字节期间稳定，切 DC 前要等 BSY 清零。
 *  - DMA 完成中断只代表 DMA 喂完 FIFO，最后一个字节可能还在移位；
 *    抬 CS 或切 DC 之前必须再等一次 BSY。
 */
#pragma once

#include <cstdint>

#include "stm32f4xx_hal.h"

namespace drv {

class SpiBus
{
public:
    /* 运行时覆盖 CubeMX 的分频，换屏时不用重新生成。
     * APB2 = 84MHz：/8=10.5M  /16=5.25M  /32=2.625M  /64=1.31M */
    bool init(uint32_t prescaler);

    /* CS 全程拉低（等价于把 CS 焊死接地）。总线上只有一个器件，
     * 没有分时复用的必要，也避开了「命令与其参数之间抬 CS 会被
     * 控制器当成命令中止」这个坑。 */
    void select_forever();

    void cmd(uint8_t c);                        /* DC=0 */
    void data(uint8_t d);                       /* DC=1，单字节 */
    void buf(const uint8_t *p, uint16_t n);     /* DC=1，走 DMA */
    void fill16(uint16_t v, uint32_t n);        /* DC=1，重复同一个 16 位值 */

    void reset_pulse(uint32_t low_ms, uint32_t settle_ms);
    void invalidate_dc() { dc_state_ = 0xff; }

    void notify_tx_done_from_isr(int32_t *higher_prio_woken);

    uint32_t tx_count() const { return tx_count_; }
    uint32_t tx_err()   const { return tx_err_; }

private:
    void set_dc(bool data);
    void wait_bsy_clear();
    void xfer(const uint8_t *p, uint16_t n);

    uint32_t tx_count_ = 0;
    uint32_t tx_err_   = 0;
    uint8_t  dc_state_ = 0xff;
    void    *tx_done_  = nullptr;
};

extern SpiBus spi_bus;

} // namespace drv
