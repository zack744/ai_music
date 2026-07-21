#include "input.h"

void Input::begin() {
    pinMode(BTN_UP_PIN,   INPUT_PULLUP);
    pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
    pinMode(BTN_OK_PIN,   INPUT_PULLUP);
}

InputEvent Input::update(BtnState &b, bool longPressBack) {
    bool reading = (digitalRead(b.pin) == LOW);   // 低电平有效
    if (reading != b.lastReading) {
        b.lastReading  = reading;
        b.lastChangeMs = millis();
    }
    // 去抖：电平稳定超过 DEBOUNCE_MS 才采信
    if ((millis() - b.lastChangeMs) < BTN_DEBOUNCE_MS) {
        return InputEvent::None;
    }

    if (reading != b.stable) {
        // 状态翻转
        b.stable = reading;
        if (b.stable) {
            // 刚按下
            b.pressStartMs = millis();
            b.longFired    = false;
        } else {
            // 刚松开：若未触发长按，则产生短按事件
            if (!b.longFired) {
                if (b.pin == BTN_UP_PIN)        return InputEvent::Up;
                else if (b.pin == BTN_DOWN_PIN) return InputEvent::Down;
                else                            return InputEvent::Ok;
            }
        }
    } else if (b.stable && longPressBack && !b.longFired) {
        // 持续按下且超过长按阈值 -> 长按事件（仅 Back 键）
        if ((millis() - b.pressStartMs) >= BTN_LONGPRESS_MS) {
            b.longFired = true;
            return InputEvent::Back;
        }
    }
    return InputEvent::None;
}

InputEvent Input::poll() {
    InputEvent e;
    e = update(_up, true);          // BOOT 支持长按返回
    if (e != InputEvent::None) return e;
    e = update(_down, false);
    if (e != InputEvent::None) return e;
    e = update(_ok, false);
    return e;
}
