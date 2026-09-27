/*
 * key.hpp — 用户按键（PB2，内部上拉，按下为低）
 *
 * PB2 同时是 BOOT1。BOOT1 只在复位瞬间被采样，而开发板 A 的 BOOT0 固定拉低，
 * 所以无论按键当时什么状态都从 Flash 启动——当普通输入用是安全的。
 *
 * 用轮询+计数消抖，不用 EXTI：按键本来就不需要低延迟响应，
 * 而 EXTI 在抖动期间会打出一串中断，反而要在中断里再做一次消抖。
 * 在周期任务里以固定间隔调用 poll()，消抖时间 = kDebounceTicks * 调用周期。
 */
#pragma once

#include <cstdint>

namespace drv {

class Key
{
public:
    /* 连续 3 次采样一致才认账。按 10ms 周期调用即 30ms 消抖。 */
    static constexpr uint8_t kDebounceTicks = 3;

    /* 按住超过这么多次采样算长按（10ms 周期 -> 800ms） */
    static constexpr uint16_t kLongPressTicks = 80;

    void init();

    /* 在周期任务里按固定间隔调用 */
    void poll();

    bool pressed() const { return stable_; }

    /* 边沿事件，读取即清除 —— 避免调用方自己维护上一次状态 */
    bool take_click();        /* 短按抬起 */
    bool take_long_press();   /* 按住达到长按阈值时触发一次 */

private:
    bool     raw_prev_   = false;
    bool     stable_     = false;
    uint8_t  settle_     = 0;
    uint16_t held_       = 0;
    bool     click_      = false;
    bool     long_       = false;
    bool     long_fired_ = false;
};

extern Key key;

} // namespace drv
