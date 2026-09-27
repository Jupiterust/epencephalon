#include "key.hpp"
#include "main.h"

namespace drv {

Key key;

void Key::init()
{
    /* 方向和上拉由 MX_GPIO_Init 配好，这里只同步一次初始状态，
     * 免得上电瞬间误报一次边沿 */
    stable_ = (HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) == GPIO_PIN_RESET);
    raw_prev_ = stable_;
    settle_ = 0;
    held_ = 0;
    click_ = long_ = long_fired_ = false;
}

void Key::poll()
{
    /* 上拉 + 按键接地：按下读到低电平 */
    const bool raw = (HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) == GPIO_PIN_RESET);

    if (raw != raw_prev_)
    {
        raw_prev_ = raw;
        settle_   = 0;              /* 电平变了，重新计数 */
        return;
    }

    if (settle_ < kDebounceTicks)
    {
        if (++settle_ < kDebounceTicks)
            return;
    }
    else if (raw == stable_)
    {
        /* 稳定按住期间，累计长按计时 */
        if (stable_ && !long_fired_ && ++held_ >= kLongPressTicks)
        {
            long_       = true;
            long_fired_ = true;
        }
        return;
    }

    /* 走到这里说明消抖通过且与当前稳定态不同 —— 认定为一次真实跳变 */
    if (raw != stable_)
    {
        stable_ = raw;
        if (stable_)
        {
            held_       = 0;
            long_fired_ = false;
        }
        else
        {
            /* 抬起：没触发过长按才算短按 */
            if (!long_fired_)
                click_ = true;
        }
    }
}

bool Key::take_click()
{
    const bool v = click_;
    click_ = false;
    return v;
}

bool Key::take_long_press()
{
    const bool v = long_;
    long_ = false;
    return v;
}

} // namespace drv
