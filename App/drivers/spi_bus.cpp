#include "spi_bus.hpp"

#include "main.h"
#include "spi.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "semphr.h"

namespace drv {

SpiBus spi_bus;

namespace {
/* 重复填充用的行缓冲：160 个 16 位值 = 320 字节，放 .bss 不占任务栈 */
constexpr uint16_t kFillUnits = 160;
uint8_t g_fill[kFillUnits * 2];
} // namespace

bool SpiBus::init(uint32_t prescaler)
{
    if (tx_done_ == nullptr)
    {
        tx_done_ = xSemaphoreCreateBinary();
        if (tx_done_ == nullptr)
            return false;
    }

    if (hspi1.Init.BaudRatePrescaler != prescaler)
    {
        hspi1.Init.BaudRatePrescaler = prescaler;
        if (HAL_SPI_Init(&hspi1) != HAL_OK)
            return false;
    }

    dc_state_ = 0xff;
    return true;
}

void SpiBus::select_forever()
{
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);
}

void SpiBus::reset_pulse(uint32_t low_ms, uint32_t settle_ms)
{
    HAL_GPIO_WritePin(OLED_RST_GPIO_Port, OLED_RST_Pin, GPIO_PIN_SET);
    osDelay(pdMS_TO_TICKS(10));
    HAL_GPIO_WritePin(OLED_RST_GPIO_Port, OLED_RST_Pin, GPIO_PIN_RESET);
    osDelay(pdMS_TO_TICKS(low_ms));
    HAL_GPIO_WritePin(OLED_RST_GPIO_Port, OLED_RST_Pin, GPIO_PIN_SET);
    osDelay(pdMS_TO_TICKS(settle_ms));
    invalidate_dc();        /* 复位后面板侧状态未知，强制下次真写 DC */
}

void SpiBus::wait_bsy_clear()
{
    while (__HAL_SPI_GET_FLAG(&hspi1, SPI_FLAG_BSY)) { }
}

void SpiBus::set_dc(bool data)
{
    if (dc_state_ == static_cast<uint8_t>(data))
        return;
    wait_bsy_clear();       /* 让在途字节先落地，再动 DC */
    HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin,
                      data ? GPIO_PIN_SET : GPIO_PIN_RESET);
    dc_state_ = static_cast<uint8_t>(data);
}

void SpiBus::xfer(const uint8_t *p, uint16_t n)
{
    if (tx_done_ != nullptr && n >= 16 &&
        HAL_SPI_Transmit_DMA(&hspi1, const_cast<uint8_t *>(p), n) == HAL_OK)
    {
        if (xSemaphoreTake(static_cast<SemaphoreHandle_t>(tx_done_),
                           pdMS_TO_TICKS(100)) == pdTRUE)
            ++tx_count_;
        else
            ++tx_err_;      /* 完成中断没来：查 DMA2_Stream5 中断使能 */
        return;
    }

    if (HAL_SPI_Transmit(&hspi1, const_cast<uint8_t *>(p), n, 100) == HAL_OK)
        ++tx_count_;
    else
        ++tx_err_;
}

void SpiBus::cmd(uint8_t c)   { set_dc(false); xfer(&c, 1); }
void SpiBus::data(uint8_t d)  { set_dc(true);  xfer(&d, 1); }

void SpiBus::buf(const uint8_t *p, uint16_t n)
{
    if (n == 0) return;
    set_dc(true);
    xfer(p, n);
    wait_bsy_clear();       /* 调用方随后多半要切 DC */
}

void SpiBus::fill16(uint16_t v, uint32_t n)
{
    if (n == 0) return;
    const uint8_t hi = static_cast<uint8_t>(v >> 8);
    const uint8_t lo = static_cast<uint8_t>(v & 0xff);
    for (uint16_t i = 0; i < kFillUnits; ++i)
    {
        g_fill[i * 2] = hi;
        g_fill[i * 2 + 1] = lo;
    }

    set_dc(true);
    while (n > 0)
    {
        const uint16_t batch = (n > kFillUnits) ? kFillUnits
                                                : static_cast<uint16_t>(n);
        xfer(g_fill, static_cast<uint16_t>(batch * 2));
        n -= batch;
    }
    wait_bsy_clear();
}

void SpiBus::notify_tx_done_from_isr(int32_t *higher_prio_woken)
{
    if (tx_done_ == nullptr) return;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(tx_done_), &woken);
    *higher_prio_woken = woken;
}

} // namespace drv

/* 整个工程唯一的一份。两个屏驱动共用同一条 SPI，回调不能各写各的。 */
extern "C" void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != SPI1)
        return;
    int32_t woken = 0;
    drv::spi_bus.notify_tx_done_from_isr(&woken);
    portYIELD_FROM_ISR(woken);
}
