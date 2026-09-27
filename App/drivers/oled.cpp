#include "oled.hpp"
#include "spi_bus.hpp"
#include "oledfont.h"

#include "main.h"
#include "cmsis_os.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace drv {

Oled oled;

namespace {
/* SH1106 手册给的串行时钟周期下限 250ns => 4MHz 上限。
 * APB2 84MHz /32 = 2.625MHz，是在规格内最快的一档。 */
constexpr uint32_t kPrescaler = SPI_BAUDRATEPRESCALER_32;
} // namespace

void Oled::set_pos(uint8_t x, uint8_t y)
{
    x = static_cast<uint8_t>(x + kColOffset);
    spi_bus.cmd(static_cast<uint8_t>(0xb0 + y));                /* 页地址 */
    spi_bus.cmd(static_cast<uint8_t>(((x & 0xf0) >> 4) | 0x10));/* 列高位 */
    /* 低位列地址命令是 0x00-0x0F，取低半字节。用 (x & 0xf0) 会吞掉 kColOffset。 */
    spi_bus.cmd(static_cast<uint8_t>(x & 0x0f));                /* 列低位 */
}

bool Oled::init()
{
    if (!spi_bus.init(kPrescaler))
        return false;
    spi_bus.select_forever();

    invalidate();   /* 面板即将复位，缓存里「已发送」的内容全部作废 */
    spi_bus.reset_pulse(50, 50);

    static const uint8_t kInit[] = {
        0xae,               /* 关显示 */
        0x00, 0x10,         /* 列地址低/高位归零 */
        0x40,               /* 起始行 0 */
        0x81, 0xcf,         /* 对比度 */
        0xa1,               /* 列映射反向 */
        0xc8,               /* 行扫描反向 */
        0xa6,               /* 正常显示（非反白） */
        0xa8, 0x3f,         /* 多路复用 1/64 */
        0xd3, 0x00,         /* 显示偏移 0 */
        0xd5, 0x80,         /* 时钟分频/振荡频率 */
        0xd9, 0xf1,         /* 预充电 15 时钟，放电 1 时钟 */
        0xda, 0x12,         /* COM 引脚硬件配置 */
        0xdb, 0x40,         /* VCOMH 取消选择电平 */
        0x20, 0x02,         /* 页寻址模式 */
        0x8d, 0x14,         /* 电荷泵使能 */
        0xa4,               /* 显示跟随 RAM（非全亮） */
        0xa6,               /* 非反白 */
        0xaf,               /* 开显示 */
    };
    for (uint8_t c : kInit)
        spi_bus.cmd(c);

    clear(Pen::Clear);
    set_pos(0, 0);
    return true;
}

void Oled::display_on()
{
    spi_bus.cmd(0x8d); spi_bus.cmd(0x14); spi_bus.cmd(0xaf);
}

void Oled::display_off()
{
    spi_bus.cmd(0x8d); spi_bus.cmd(0x10); spi_bus.cmd(0xae);
}

void Oled::lamp_test(uint32_t ms)
{
    spi_bus.cmd(0x81); spi_bus.cmd(0xff);   /* 对比度拉满 */
    spi_bus.cmd(0xa5);                      /* entire display ON，忽略 GRAM */
    osDelay(pdMS_TO_TICKS(ms));
    spi_bus.cmd(0xa4);                      /* 恢复为跟随 GRAM */
    spi_bus.cmd(0x81); spi_bus.cmd(0xcf);   /* 对比度复原 */
    invalidate();
}

uint8_t Oled::refresh()
{
    uint8_t pushed = 0;

    for (uint8_t page = 0; page < kPages; ++page)
    {
        /* GRAM 是 [列][页]，取一页是跨步读；先聚拢成连续缓冲，
         * 才能一次突发发完，而不是 128 次单字节写。 */
        for (uint8_t x = 0; x < kWidth; ++x)
            page_buf_[x] = gram_[x][page];

        /* 128 字节 memcmp 约 0.5us，128 字节 SPI 约 390us——永远值得比一下 */
        if (sent_valid_ && std::memcmp(page_buf_, sent_[page], kWidth) == 0)
            continue;

        std::memcpy(sent_[page], page_buf_, kWidth);

        set_pos(0, page);                   /* 3 个命令字节，DC=0 */
        spi_bus.buf(page_buf_, kWidth);     /* DC=1，DMA 突发 */
        ++pushed;
    }

    sent_valid_ = true;
    return pushed;
}

void Oled::clear(Pen pen)
{
    for (uint8_t x = 0; x < kWidth; ++x)
        for (uint8_t p = 0; p < kPages; ++p)
        {
            if (pen == Pen::Write)          gram_[x][p] = 0xff;
            else if (pen == Pen::Clear)     gram_[x][p] = 0x00;
            else                            gram_[x][p] = static_cast<uint8_t>(~gram_[x][p]);
        }
}

void Oled::draw_point(int x, int y, Pen pen)
{
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight)
        return;

    const uint8_t page = static_cast<uint8_t>(y / 8);
    const uint8_t bit  = static_cast<uint8_t>(y % 8);

    if (pen == Pen::Write)
        gram_[x][page] |= static_cast<uint8_t>(1u << bit);
    else if (pen == Pen::Inversion)
        gram_[x][page] ^= static_cast<uint8_t>(1u << bit);
    else
        gram_[x][page] &= static_cast<uint8_t>(~(1u << bit));
}

void Oled::draw_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, Pen pen)
{
    if (y1 == y2)
    {
        const uint8_t s = (x1 <= x2) ? x1 : x2;
        const uint8_t e = (x1 <= x2) ? x2 : x1;
        for (uint8_t x = s; x <= e; ++x)
            draw_point(x, y1, pen);
        return;
    }
    if (x1 == x2)
    {
        const uint8_t s = (y1 <= y2) ? y1 : y2;
        const uint8_t e = (y1 <= y2) ? y2 : y1;
        for (uint8_t y = s; y <= e; ++y)
            draw_point(x1, y, pen);
        return;
    }

    /* 原 C 版这里是 (x1<=x2) ? (x_ed=x2) : (x_ed=x2)——两个分支都取 x2，
     * 于是 x1>x2 的斜线画不出来。这里修正为取较大者。 */
    const float k = static_cast<float>(y2 - y1) / static_cast<float>(x2 - x1);
    const float b = static_cast<float>(y1) - k * static_cast<float>(x1);

    const uint8_t s = (x1 <= x2) ? x1 : x2;
    const uint8_t e = (x1 <= x2) ? x2 : x1;
    for (uint8_t x = s; x <= e; ++x)
        draw_point(x, static_cast<int>(k * static_cast<float>(x) + b), pen);
}

void Oled::show_char(uint8_t row, uint8_t col, char ch)
{
    if (row >= kRows || col >= kCols || ch < ' ')
        return;

    const uint8_t x0  = static_cast<uint8_t>(col * kCharW);
    const uint8_t y0  = static_cast<uint8_t>(row * kCharH);
    const uint8_t idx = static_cast<uint8_t>(ch - ' ');

    uint8_t x = x0, y = y0;
    for (uint8_t t = 0; t < kCharH; ++t)
    {
        uint8_t temp = asc2_1206[idx][t];
        for (uint8_t b = 0; b < 8; ++b)
        {
            draw_point(x, y, (temp & 0x80) ? Pen::Write : Pen::Clear);
            temp = static_cast<uint8_t>(temp << 1);
            ++y;
            if (y - y0 == kCharH)
            {
                y = y0;
                ++x;
                break;
            }
        }
    }
}

void Oled::show_string(uint8_t row, uint8_t col, const char *s)
{
    if (s == nullptr) return;
    for (uint8_t c = col; *s != '\0' && c < kCols; ++c, ++s)
        show_char(row, c, *s);
}

void Oled::printf(uint8_t row, uint8_t col, const char *fmt, ...)
{
    if (row >= kRows || col >= kCols)
        return;

    char buf[kCols + 1];
    va_list ap;
    va_start(ap, fmt);
    /* vsnprintf 而非原版的 vsprintf：栈上缓冲，越界就不是显示问题而是崩溃 */
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    buf[kCols - col] = '\0';    /* 超出本行的部分截断 */
    show_string(row, col, buf);
}

} // namespace drv
