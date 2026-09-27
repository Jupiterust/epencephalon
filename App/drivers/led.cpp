#include "led.hpp"
#include "main.h"

namespace drv {

Led led;

namespace {
struct PinDef { GPIO_TypeDef *port; uint16_t pin; };

/* 顺序即 A..H，也就是物理上从第一颗排到最后一颗。
 * CubeMX 的 LED_8 在物理第一位，所以这张表是倒着写的。 */
const PinDef kPins[Led::kCount] = {
    { LED_8_GPIO_Port, LED_8_Pin },     /* A  PG8 */
    { LED_7_GPIO_Port, LED_7_Pin },     /* B  PG7 */
    { LED_6_GPIO_Port, LED_6_Pin },     /* C  PG6 */
    { LED_5_GPIO_Port, LED_5_Pin },     /* D  PG5 */
    { LED_4_GPIO_Port, LED_4_Pin },     /* E  PG4 */
    { LED_3_GPIO_Port, LED_3_Pin },     /* F  PG3 */
    { LED_2_GPIO_Port, LED_2_Pin },     /* G  PG2 */
    { LED_1_GPIO_Port, LED_1_Pin },     /* H  PG1 */
};

inline GPIO_PinState level_for(bool on)
{
    const bool high = Led::kActiveLow ? !on : on;
    return high ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
} // namespace

void Led::init()
{
    /* 方向由 MX_GPIO_Init 配好。上电初值也在 CubeMX 里设成了「灭」，
     * 这里再写一次只是把软件侧的 mask_ 和硬件对齐。 */
    write_mask(0);
}

void Led::set(LedId id, bool on)
{
    const uint8_t i = static_cast<uint8_t>(id);
    if (i >= kCount)
        return;
    HAL_GPIO_WritePin(kPins[i].port, kPins[i].pin, level_for(on));
    if (on) mask_ |=  static_cast<uint8_t>(1u << i);
    else    mask_ &= static_cast<uint8_t>(~(1u << i));
}

void Led::toggle(LedId id)
{
    const uint8_t i = static_cast<uint8_t>(id);
    if (i >= kCount)
        return;
    set(id, (mask_ & (1u << i)) == 0);
}

void Led::all(bool on)
{
    write_mask(on ? 0xff : 0x00);
}

void Led::write_mask(uint8_t mask)
{
    for (uint8_t i = 0; i < kCount; ++i)
        HAL_GPIO_WritePin(kPins[i].port, kPins[i].pin,
                          level_for((mask & (1u << i)) != 0));
    mask_ = mask;
}

} // namespace drv
