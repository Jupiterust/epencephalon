/*
 * led.hpp — 开发板 A 的 8 路 LED
 *
 * 编号用字母 A..H，A 是物理上的第一颗。
 *
 * 注意：CubeMX 里的标签 LED_1..LED_8 对应 PG1..PG8，但物理排列是反的
 * ——PG8 才是第一颗。所以这里 A 映射到 LED_8_Pin，H 映射到 LED_1_Pin。
 * 改用字母就是为了避免「1 号到底指哪颗」这种歧义。
 *
 * 极性：kActiveLow = true 表示引脚拉低点亮（共阳接法）。
 */
#pragma once

#include <cstdint>

namespace drv {

enum class LedId : uint8_t { A = 0, B, C, D, E, F, G, H };

class Led
{
public:
    static constexpr uint8_t kCount     = 8;
    static constexpr bool    kActiveLow = true;

    void init();                            /* 全灭 */

    void set(LedId id, bool on);
    void toggle(LedId id);
    void all(bool on);

    /* 位图一次刷 8 路：bit0 -> A，bit7 -> H。跑马灯/状态条用这个。 */
    void write_mask(uint8_t mask);

    uint8_t mask() const { return mask_; }

private:
    uint8_t mask_ = 0;
};

extern Led led;

} // namespace drv
