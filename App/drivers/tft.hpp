/*
 * tft.hpp — 1.8 寸 128x160 彩屏（ILI9163B）驱动
 *
 * 【当前未启用】手上这块 1.8 寸屏点不亮，换回 1.3 寸 SH1106 后同一套接线
 * 立刻正常，所以基本可以判定是那块屏本身的问题，不是驱动或接线。
 * 代码保留，等换一块新的 TFT 再试：把 app_main.cpp 里 kUseTft 改成 true 即可。
 *
 * 移植自龙邱 LQ_SGP18T 参考代码。原版用 GPIO 软件模拟 SPI 且 CS 常拉低，
 * 这里换成硬件 SPI1 + DMA，CS 由 PA6 真实驱动。
 *
 * 显示方向由 init() 的 MADCTL(0x36) 决定：
 *   横屏 0xA0 -> 160 宽 x 128 高
 *   竖屏 0xC0 -> 128 宽 x 160 高
 * 驱动内部 RAM 是 162x132，比可见区大，清屏按 RAM 尺寸刷以免边缘残留。
 *
 * 没有帧缓冲：全屏 160x128x2 = 40KB，在 10.5MHz 下推一次要 31ms，
 * 做帧缓冲既费 RAM 又不会更快。所有绘制都是直接写面板，
 * 因此刷新的代价正比于改动面积——只重画变化的区域。
 */
#pragma once

#include <cstdint>

namespace drv {

/* RGB565 */
namespace color {
constexpr uint16_t kBlack  = 0x0000;
constexpr uint16_t kWhite  = 0xffff;
constexpr uint16_t kRed    = 0xf800;
constexpr uint16_t kGreen  = 0x07e0;
constexpr uint16_t kBlue   = 0x001f;
constexpr uint16_t kYellow = 0xffe0;
constexpr uint16_t kCyan   = 0x07ff;
constexpr uint16_t kPurple = 0xf81f;
constexpr uint16_t kOrange = 0xfc08;
constexpr uint16_t kGray   = 0x8410;
} // namespace color

class Tft
{
public:
    /* 驱动 IC 的 RAM 尺寸，比可见区大 */
    static constexpr uint8_t kRamW = 162;
    static constexpr uint8_t kRamH = 132;

    /* 横屏下的可见尺寸 */
    static constexpr uint8_t kWidth  = 160;
    static constexpr uint8_t kHeight = 128;

    /* 6x8 字体的字符格数（横屏） */
    static constexpr uint8_t kCols6x8 = kWidth / 6;    /* 26 */
    static constexpr uint8_t kRows6x8 = kHeight / 8;   /* 16 */

    /* 8x16 字体的字符格数（横屏） */
    static constexpr uint8_t kCols8x16 = kWidth / 8;   /* 20 */
    static constexpr uint8_t kRows8x16 = kHeight / 16; /* 8  */

    bool init(bool portrait = false);

    void clear(uint16_t color);
    void fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color);
    void draw_pixel(uint8_t x, uint8_t y, uint16_t color);
    void draw_hline(uint8_t x, uint8_t y, uint8_t w, uint16_t color);
    void draw_vline(uint8_t x, uint8_t y, uint8_t h, uint16_t color);
    void draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color);

    /* 坐标单位是字符格，不是像素 */
    void char6x8(uint8_t col, uint8_t row, char ch, uint16_t fg, uint16_t bg);
    void str6x8(uint8_t col, uint8_t row, const char *s, uint16_t fg, uint16_t bg);
    void printf6x8(uint8_t col, uint8_t row, uint16_t fg, uint16_t bg,
                   const char *fmt, ...);

    void char8x16(uint8_t col, uint8_t row, char ch, uint16_t fg, uint16_t bg);
    void str8x16(uint8_t col, uint8_t row, const char *s, uint16_t fg, uint16_t bg);
    void printf8x16(uint8_t col, uint8_t row, uint16_t fg, uint16_t bg,
                    const char *fmt, ...);

    /* 点亮全屏红->绿->蓝，用来确认面板、接线和颜色顺序。 */
    void lamp_test(uint32_t ms_each);

    /* 排线自检，永不返回。把 SCK/MOSI/DC/RST/CS 从 SPI 手里夺回来当普通
     * 推挽输出，五根线一起每秒翻转一次。拿万用表在**模块那一端**量，
     * 每根都应在 0V <-> 3.3V 之间摆动。不用示波器就能同时证明
     * 引脚映射、排线通断和接插方向。只在调试台上用。 */
    [[noreturn]] void pin_test();

    /* 分阶段诊断，永不返回，不需要仪器，光看屏就能定位故障。
     * 每 3 秒换一个阶段，循环：
     *   1 RST 拉低      面板被按在复位里
     *   2 RST 释放      出复位但未初始化
     *   3 初始化 + 关显示(0x28)
     *   4 开显示(0x29) + 全屏红
     *   5 全屏蓝
     * 判读方式见调用处注释。 */
    [[noreturn]] void diag_loop();

    /* 引脚映射测试，永不返回。一次只翻转一根线，用板上 LED 指示是哪一根，
     * 从而把「模块某个脚有反应」唯一对应到一个 MCU 引脚。
     *
     *   LED1 -> PB3  (SPI1_SCK)     LED5 -> PB9  (DC)
     *   LED2 -> PA7  (SPI1_MOSI)    LED6 -> PB10 (RST)
     *   LED3 -> PB13 (SPI2_SCK)     LED7 -> PA6  (CS)
     *   LED4 -> PB15 (SPI2_MOSI)
     *
     * 每阶段 4 秒，翻转的引脚万用表直流档读约 1.6V，其余读 0V。 */
    [[noreturn]] void bus_test();

    bool portrait() const { return portrait_; }
    uint8_t width()  const { return portrait_ ? kHeight : kWidth; }
    uint8_t height() const { return portrait_ ? kWidth  : kHeight; }

private:
    void set_window(uint8_t xs, uint8_t ys, uint8_t xe, uint8_t ye);

    bool portrait_ = false;
};

extern Tft tft;

} // namespace drv
