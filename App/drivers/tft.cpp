#include "tft.hpp"
#include "spi_bus.hpp"
#include "tft_font.h"
#include "led.hpp"

#include "main.h"
#include "spi.h"
#include "cmsis_os.h"

#include <cstdarg>
#include <cstdio>

namespace drv {

Tft tft;

namespace {

/* 一个 8x16 字符 = 128 像素 = 256 字节，够放下本文件里最大的一次批量写。
 * 放 .bss 而不是栈上：app 任务的栈只有 2560 字节。 */
uint8_t g_cell[8 * 16 * 2];

inline void put_px(uint8_t *&p, uint16_t c)
{
    *p++ = static_cast<uint8_t>(c >> 8);
    *p++ = static_cast<uint8_t>(c & 0xff);
}

/* 参考驱动的初始化序列，逐字搬过来。
 * 格式：命令后面跟 N 个参数，用 {cmd, n, args...} 编码。 */
const uint8_t kInit[] = {
    0x11, 0,                                    /* 退出睡眠 */
    0x3a, 1, 0x55,                              /* 16 位/像素 (RGB565) */
    0x26, 1, 0x04,                              /* gamma 曲线选择 */
    0xf2, 1, 0x01,
    0xe0, 15, 0x3f, 0x25, 0x1c, 0x1e, 0x20, 0x12, 0x2a, 0x90,
              0x24, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00,   /* 正 gamma */
    0xe1, 15, 0x20, 0x20, 0x20, 0x20, 0x05, 0x00, 0x15, 0xa7,
              0x3d, 0x18, 0x25, 0x2a, 0x2b, 0x2b, 0x3a,   /* 负 gamma */
    0xb1, 2, 0x00, 0x00,                        /* 刷新率 */
    0xb4, 1, 0x07,                              /* 反转控制 */
    0xc0, 2, 0x0a, 0x02,                        /* 电源控制 1 */
    0xc1, 1, 0x02,                              /* 电源控制 2 */
    0xc5, 2, 0x4f, 0x5a,                        /* VCOM 控制 */
    0xc7, 1, 0x40,
    0x2a, 4, 0x00, 0x00, 0x00, 0xa8,            /* 列地址范围 */
    0x2b, 4, 0x00, 0x00, 0x00, 0xb3,            /* 行地址范围 */
};

} // namespace

/* 调用方必须已经 begin()：0x2A/0x2B 的参数和随后的 0x2C 像素流
 * 必须处在同一个 CS 低电平期内，中途抬 CS 会让命令被丢弃。 */
void Tft::set_window(uint8_t xs, uint8_t ys, uint8_t xe, uint8_t ye)
{
    spi_bus.cmd(0x2a);
    spi_bus.data(0x00); spi_bus.data(xs);
    spi_bus.data(0x00); spi_bus.data(xe);

    spi_bus.cmd(0x2b);
    spi_bus.data(0x00); spi_bus.data(ys);
    spi_bus.data(0x00); spi_bus.data(ye);

    spi_bus.cmd(0x2c);      /* 之后写入的都是 GRAM 像素 */
}

bool Tft::init(bool portrait)
{
    /* ILI9163 可以跑到 10MHz 以上；先用保守值，稳定后再提。 */
    if (!spi_bus.init(SPI_BAUDRATEPRESCALER_16))
        return false;
    spi_bus.select_forever();

    portrait_ = portrait;

    /* 数据手册要求复位脉冲 >=10us，释放后 >=120ms 才能收命令 */
    spi_bus.reset_pulse(50, 120);

    for (size_t i = 0; i < sizeof(kInit);)
    {
        const uint8_t c = kInit[i++];
        const uint8_t n = kInit[i++];
        
        spi_bus.cmd(c);
        for (uint8_t k = 0; k < n; ++k)
            spi_bus.data(kInit[i++]);
        
        /* Sleep Out 之后必须等 120ms：振荡器和升压电路要时间稳定，
         * 这期间发过去的配置命令会被丢掉。参考代码写的 10 其实是
         * 裸 NOP 循环，在 600MHz 的 RT1052 上只有十几微秒，
         * 照抄那个数字反而比它原意更短。 */
        if (c == 0x11)
            osDelay(pdMS_TO_TICKS(120));
    }

    /* MADCTL：决定扫描方向，也就决定了横屏还是竖屏 */
    
    spi_bus.cmd(0x36);
    spi_bus.data(portrait_ ? 0xc0 : 0xa0);
    spi_bus.cmd(0xb7);
    spi_bus.data(0x00);
    spi_bus.cmd(0x29);          /* 开显示 */
    
    osDelay(pdMS_TO_TICKS(20));

    clear(color::kBlack);
    return true;
}

void Tft::clear(uint16_t c)
{
    /* 按驱动 RAM 尺寸刷，比可见区大，避免边缘留下上电噪点 */
    
    set_window(0, 0, kRamW - 1, kRamH - 1);
    spi_bus.fill16(c, static_cast<uint32_t>(kRamW) * kRamH);
    
}

void Tft::fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t c)
{
    if (w == 0 || h == 0)
        return;
    
    set_window(x, y, static_cast<uint8_t>(x + w - 1), static_cast<uint8_t>(y + h - 1));
    spi_bus.fill16(c, static_cast<uint32_t>(w) * h);
    
}

void Tft::draw_pixel(uint8_t x, uint8_t y, uint16_t c)
{
    
    set_window(x, y, x, y);
    spi_bus.fill16(c, 1);
    
}

void Tft::draw_hline(uint8_t x, uint8_t y, uint8_t w, uint16_t c)
{
    fill_rect(x, y, w, 1, c);
}

void Tft::draw_vline(uint8_t x, uint8_t y, uint8_t h, uint16_t c)
{
    fill_rect(x, y, 1, h, c);
}

void Tft::draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t c)
{
    if (w == 0 || h == 0)
        return;
    draw_hline(x, y, w, c);
    draw_hline(x, static_cast<uint8_t>(y + h - 1), w, c);
    draw_vline(x, y, h, c);
    draw_vline(static_cast<uint8_t>(x + w - 1), y, h, c);
}

void Tft::char6x8(uint8_t col, uint8_t row, char ch, uint16_t fg, uint16_t bg)
{
    if (ch < ' ' || static_cast<uint8_t>(ch) > 0x7e)
        ch = ' ';
    const uint8_t *glyph = kFont6x8[static_cast<uint8_t>(ch) - 32];

    /* 字模是列主序、bit0 在最上面一行；窗口按行递增，所以外层走行 */
    uint8_t *p = g_cell;
    for (uint8_t j = 0; j < 8; ++j)
        for (uint8_t i = 0; i < 6; ++i)
            put_px(p, (glyph[i] & (1u << j)) ? fg : bg);

    const uint8_t x = static_cast<uint8_t>(col * 6);
    const uint8_t y = static_cast<uint8_t>(row * 8);
    
    set_window(x, y, static_cast<uint8_t>(x + 5), static_cast<uint8_t>(y + 7));
    spi_bus.buf(g_cell, 6 * 8 * 2);
    
}

void Tft::str6x8(uint8_t col, uint8_t row, const char *s, uint16_t fg, uint16_t bg)
{
    if (s == nullptr)
        return;
    for (uint8_t c = col; *s != '\0' && c < kCols6x8; ++c, ++s)
        char6x8(c, row, *s, fg, bg);
}

void Tft::printf6x8(uint8_t col, uint8_t row, uint16_t fg, uint16_t bg,
                    const char *fmt, ...)
{
    char buf[kCols6x8 + 1];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    str6x8(col, row, buf, fg, bg);
}

void Tft::char8x16(uint8_t col, uint8_t row, char ch, uint16_t fg, uint16_t bg)
{
    if (ch < ' ' || static_cast<uint8_t>(ch) > 0x7e)
        ch = ' ';
    const uint8_t *glyph = kFont8x16[static_cast<uint8_t>(ch) - 32];

    /* 8x16 是**行主序**：16 个字节各代表一行，bit i 对应第 i 列（bit0 在左）。
     * 注意这和 6x8 字库相反——那个是列主序、bit 代表行。两张表来自同一份
     * 参考驱动却用了不同布局，照抄一种会让另一种变成乱码。 */
    uint8_t *p = g_cell;
    for (uint8_t j = 0; j < 16; ++j)        /* j = 行 */
        for (uint8_t i = 0; i < 8; ++i)     /* i = 列 */
            put_px(p, (glyph[j] & (1u << i)) ? fg : bg);

    const uint8_t x = static_cast<uint8_t>(col * 8);
    const uint8_t y = static_cast<uint8_t>(row * 16);
    
    set_window(x, y, static_cast<uint8_t>(x + 7), static_cast<uint8_t>(y + 15));
    spi_bus.buf(g_cell, 8 * 16 * 2);
    
}

void Tft::str8x16(uint8_t col, uint8_t row, const char *s, uint16_t fg, uint16_t bg)
{
    if (s == nullptr)
        return;
    for (uint8_t c = col; *s != '\0' && c < kCols8x16; ++c, ++s)
        char8x16(c, row, *s, fg, bg);
}

void Tft::printf8x16(uint8_t col, uint8_t row, uint16_t fg, uint16_t bg,
                     const char *fmt, ...)
{
    char buf[kCols8x16 + 1];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    str8x16(col, row, buf, fg, bg);
}

void Tft::bus_test()
{
    /* SPI 外设必须先关，否则 PB3/PA7 仍归它管，写 ODR 无效 */
    __HAL_SPI_DISABLE(&hspi1);

    struct Sig { GPIO_TypeDef *port; uint16_t pin; };

    /* 顺序即 LED1..LED7。每个阶段只翻转其中一根，其余全部压低，
     * 这样模块那端任何一个脚有反应，都能唯一对应到一个 MCU 引脚。 */
    static const Sig kSig[] = {
        { GPIOB, GPIO_PIN_3  },     /* LED1  PB3   SPI1_SCK  */
        { GPIOA, GPIO_PIN_7  },     /* LED2  PA7   SPI1_MOSI */
        { GPIOB, GPIO_PIN_13 },     /* LED3  PB13  SPI2_SCK  */
        { GPIOB, GPIO_PIN_15 },     /* LED4  PB15  SPI2_MOSI */
        { GPIOB, OLED_DC_Pin },     /* LED5  PB9   DC        */
        { GPIOB, OLED_RST_Pin},     /* LED6  PB10  RST       */
        { GPIOA, TFT_CS_Pin  },     /* LED7  PA6   CS        */
    };
    constexpr uint8_t kN = sizeof(kSig) / sizeof(kSig[0]);

    GPIO_InitTypeDef g{};
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    for (const Sig &sg : kSig)
    {
        g.Pin = sg.pin;
        HAL_GPIO_Init(sg.port, &g);
    }

    for (;;)
    {
        for (uint8_t i = 0; i < kN; ++i)
        {
            /* 先把七根全部压低 */
            for (const Sig &sg : kSig)
                HAL_GPIO_WritePin(sg.port, sg.pin, GPIO_PIN_RESET);

            led.write_mask(static_cast<uint8_t>(1u << i));   /* LED(i+1) 指示 */

            /* 4 秒方波，半周期 2ms -> 250Hz，万用表直流档约读到 1.6V */
            for (uint16_t t = 0; t < 2000; ++t)
            {
                HAL_GPIO_WritePin(kSig[i].port, kSig[i].pin,
                                  (t & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
                osDelay(pdMS_TO_TICKS(2));
            }
        }
    }
}

void Tft::diag_loop()
{
    spi_bus.init(SPI_BAUDRATEPRESCALER_16);
    spi_bus.select_forever();

    for (;;)
    {
        /* 阶段 1：把面板按在复位里 */
        HAL_GPIO_WritePin(OLED_RST_GPIO_Port, OLED_RST_Pin, GPIO_PIN_RESET);
        osDelay(pdMS_TO_TICKS(3000));

        /* 阶段 2：释放复位，但什么命令都不发 */
        HAL_GPIO_WritePin(OLED_RST_GPIO_Port, OLED_RST_Pin, GPIO_PIN_SET);
        osDelay(pdMS_TO_TICKS(3000));

        /* 阶段 3：走完整初始化，然后关显示 */
        init(portrait_);
        
        spi_bus.cmd(0x28);          /* display off */
        
        osDelay(pdMS_TO_TICKS(3000));

        /* 阶段 4：开显示 + 全屏红 */
        
        spi_bus.cmd(0x29);          /* display on */
        
        clear(color::kRed);
        osDelay(pdMS_TO_TICKS(3000));

        /* 阶段 5：全屏蓝 */
        clear(color::kBlue);
        osDelay(pdMS_TO_TICKS(3000));
    }
}

void Tft::pin_test()
{
    /* 先关 SPI，否则 SCK/MOSI 仍归外设管，写 ODR 不起作用 */
    __HAL_SPI_DISABLE(&hspi1);

    GPIO_InitTypeDef g{};
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;

    g.Pin = GPIO_PIN_3;  HAL_GPIO_Init(GPIOB, &g);              /* SCK  PB3  */
    g.Pin = GPIO_PIN_7;  HAL_GPIO_Init(GPIOA, &g);              /* MOSI PA7  */
    g.Pin = OLED_DC_Pin | OLED_RST_Pin;
                         HAL_GPIO_Init(GPIOB, &g);              /* PB9 PB10  */
    g.Pin = TFT_CS_Pin;  HAL_GPIO_Init(TFT_CS_GPIO_Port, &g);   /* CS   PA6  */

    for (;;)
    {
        for (int level = 0; level < 2; ++level)
        {
            const GPIO_PinState v = level ? GPIO_PIN_SET : GPIO_PIN_RESET;
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, v);
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, v);
            HAL_GPIO_WritePin(GPIOB, OLED_DC_Pin | OLED_RST_Pin, v);
            HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, v);
            osDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void Tft::lamp_test(uint32_t ms_each)
{
    /* 三原色各刷一遍：屏亮说明供电/CS/SPI/复位/初始化全通；
     * 颜色不对则是 RGB/BGR 位序或 MADCTL 的问题，而不是链路问题。 */
    const uint16_t seq[] = { color::kRed, color::kGreen, color::kBlue };
    for (uint16_t c : seq)
    {
        clear(c);
        osDelay(pdMS_TO_TICKS(ms_each));
    }
    clear(color::kBlack);
}

} // namespace drv
