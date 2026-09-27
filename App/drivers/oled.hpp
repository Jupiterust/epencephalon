/*
 * oled.hpp — 1.3 寸 128x64 单色 OLED（SH1106）驱动
 *
 * 由 DJI RM_OLED 的 C 版本移植。以下结论都是实机调出来的，不要「简化」掉：
 *
 *  - SH1106 有 132 列 RAM，可见的 128 列居中，所以列地址要 +2（kColOffset）。
 *    若换成 SSD1306（128 列控制器），把它改成 0，否则画面右移 2 px。
 *  - 设置低位列地址的命令是 0x00-0x0F，即**低半字节**。原始 DJI 代码用
 *    (x & 0xf0) 做掩码，导致偏移被吞掉、画面左移 2 px、右边两列是残留内容。
 *  - refresh() 把 8 个页各自和「面板当前显示内容」比对，只推变化的页。
 *    整屏 1024 字节在 2.6MHz 下约 3.1ms；静止画面则一个字节都不发。
 *
 * 和最初版本的唯一区别：CS 现在是真实引脚（PA6），由 SpiBus 全程拉低，
 * 而不是模块上焊死接地。
 */
#pragma once

#include <cstdint>

namespace drv {

enum class Pen : uint8_t {
    Clear     = 0,
    Write     = 1,
    Inversion = 2,
};

class Oled
{
public:
    static constexpr uint8_t kWidth     = 128;
    static constexpr uint8_t kHeight    = 64;
    static constexpr uint8_t kPages     = kHeight / 8;
    static constexpr uint8_t kColOffset = 2;    /* SH1106 可见窗口起点 */

    /* 字模 6x12，故 21 列 x 5 行 */
    static constexpr uint8_t kCharW = 6;
    static constexpr uint8_t kCharH = 12;
    static constexpr uint8_t kCols  = kWidth / kCharW;      /* 21 */
    static constexpr uint8_t kRows  = kHeight / kCharH;     /* 5  */

    bool init();

    void display_on();
    void display_off();

    /* 只推送内容变化的页，返回实际发出的页数（0 表示画面没动）。 */
    uint8_t refresh();

    /* 面板 RAM 被驱动之外的操作扰动过时调用，作废脏页缓存。 */
    void invalidate() { sent_valid_ = false; }

    void clear(Pen pen = Pen::Clear);
    void draw_point(int x, int y, Pen pen);
    void draw_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, Pen pen);

    void show_char(uint8_t row, uint8_t col, char ch);
    void show_string(uint8_t row, uint8_t col, const char *s);
    void printf(uint8_t row, uint8_t col, const char *fmt, ...);

    /* 排错用：发 0xA5「全屏点亮」，由控制器直接驱动所有像素、不经 GRAM。
     * 亮了说明供电/CS/SPI/复位/初始化整条链路都通。 */
    void lamp_test(uint32_t ms);

private:
    void set_pos(uint8_t x, uint8_t y);

    uint8_t gram_[kWidth][kPages] = {};
    uint8_t page_buf_[kWidth]     = {};   /* 按页聚拢后一次性突发 */
    uint8_t sent_[kPages][kWidth] = {};   /* 面板当前显示内容的副本 */
    bool    sent_valid_           = false;
};

extern Oled oled;

} // namespace drv
