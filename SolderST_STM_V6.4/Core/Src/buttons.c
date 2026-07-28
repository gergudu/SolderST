/**
 * @file buttons.c
 * @brief Оптимизированная обработка кнопок (Complexity < 10)
 */

#include "buttons.h"
#include "config.h"
#include "gpio.h"

/* =======================================================================
   ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ (Выделение памяти)
   ======================================================================= */

/**
 * @brief Контекст кнопок. Определяем здесь, чтобы линковщик не ругался на undefined reference.
 */
volatile ButtonContext_t g_ButtonContext = {
    .state = BTN_STATE_IDLE,
    .stable_mask = 0,
    .buttons_tick = 0,
    .chord_window = 0
};

/* =======================================================================
   ВНУТРЕННИЕ ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
   ======================================================================= */

static inline bool IsChordPressed(uint16_t mask) {
    return (mask == (BTN_UP_PIN | BTN_DN_PIN));
}

static inline bool IsInvalidCombination(uint16_t mask) {
    uint8_t count = 0;
    for (int i = 0; i < 16; i++) {
        if (mask & (1 << i)) count++;
    }
    // Разрешены: 0 кнопок, 1 любая кнопка, или 2 конкретных (аккорд)
    return (count > 2) || (count == 2 && !IsChordPressed(mask));
}

static Button_t GetSingleButton(uint16_t mask) {
    if (mask == BTN_UP_PIN)   return BTN_UP;
    if (mask == BTN_DN_PIN)   return BTN_DN;
    if (mask == BTN_SET1_PIN) return BTN_SET1;
    if (mask == BTN_SET2_PIN) return BTN_SET2;
    if (mask == BTN_SET3_PIN) return BTN_SET3;
    if (mask == BTN_TOOL_PIN) return BTN_TOOL;
    return BTN_NONE;
}

static ButtonEvent_t MapToEvent(Button_t btn, bool is_long) {
    if (btn == BTN_UP) return is_long ? BTN_EVENT_UP_REPEAT : BTN_EVENT_UP_SHORT;
    if (btn == BTN_DN) return is_long ? BTN_EVENT_DN_REPEAT : BTN_EVENT_DN_SHORT;

    // Арифметика для SET1...SET3 и TOOL
    ButtonEvent_t base = is_long ? BTN_EVENT_SET1_LONG : BTN_EVENT_SET1_SHORT;
    return (ButtonEvent_t)(base + (btn - BTN_SET1));
}

/* =======================================================================
   ОБРАБОТЧИКИ СОСТОЯНИЙ (Декомпозиция сложности)
   ======================================================================= */

static ButtonEvent_t HandleSinglePress(bool any_pressed) {
    if (!any_pressed) {
        ButtonEvent_t e = g_ButtonContext.long_press_fired ? BTN_EVENT_NONE : MapToEvent(g_ButtonContext.pressed_button, false);
        g_ButtonContext.state = BTN_STATE_IDLE;
        return e;
    }

    if (!g_ButtonContext.long_press_fired) {
        if (g_ButtonContext.buttons_tick >= LONG_PRESS_TICKS) {
            g_ButtonContext.long_press_fired = true;
            g_ButtonContext.repeat_counter = 0;
            return MapToEvent(g_ButtonContext.pressed_button, true);
        }
    } else if (g_ButtonContext.pressed_button == BTN_UP || g_ButtonContext.pressed_button == BTN_DN) {
        if (++g_ButtonContext.repeat_counter >= REPEAT_INTERVAL_TICKS) {
            g_ButtonContext.repeat_counter = 0;
            return (g_ButtonContext.pressed_button == BTN_UP) ? BTN_EVENT_UP_REPEAT : BTN_EVENT_DN_REPEAT;
        }
    }
    return BTN_EVENT_NONE;
}

static ButtonEvent_t HandleChordWait(bool any_pressed, uint16_t gpio) {
    if (IsChordPressed(gpio)) {
        if (g_ButtonContext.chord_window > 0) {
            g_ButtonContext.state = BTN_STATE_CHORD_ACTIVE;
            g_ButtonContext.chord_long_fired = false;
        } else {
            g_ButtonContext.state = BTN_STATE_IGNORED;
        }
    } else if (!any_pressed) {
        g_ButtonContext.state = BTN_STATE_IDLE;
        return (g_ButtonContext.first_chord_btn == BTN_UP) ? BTN_EVENT_UP_SHORT : BTN_EVENT_DN_SHORT;
    } else if (g_ButtonContext.buttons_tick >= LONG_PRESS_TICKS) {
        g_ButtonContext.state = BTN_STATE_SINGLE_PRESSED;
        g_ButtonContext.pressed_button = g_ButtonContext.first_chord_btn;
        g_ButtonContext.long_press_fired = true;
        g_ButtonContext.repeat_counter = 0;
        return (g_ButtonContext.pressed_button == BTN_UP) ? BTN_EVENT_UP_REPEAT : BTN_EVENT_DN_REPEAT;
    }
    return BTN_EVENT_NONE;
}

/* =======================================================================
   ОСНОВНОЙ FSM
   ======================================================================= */

ButtonEvent_t BUTTONS_Process(void) {
    uint16_t gpio = g_ButtonContext.stable_mask;
    bool any_pressed = (gpio != 0);
    ButtonEvent_t event = BTN_EVENT_NONE;

    switch (g_ButtonContext.state) {
        case BTN_STATE_IDLE:
            if (!any_pressed) break;
            g_ButtonContext.buttons_tick = 0;
            if (IsInvalidCombination(gpio)) {
                g_ButtonContext.state = BTN_STATE_IGNORED;
            } else if (IsChordPressed(gpio)) {
                g_ButtonContext.state = BTN_STATE_CHORD_ACTIVE;
                g_ButtonContext.chord_long_fired = false;
            } else {
                Button_t btn = GetSingleButton(gpio);
                if (btn == BTN_UP || btn == BTN_DN) {
                    g_ButtonContext.state = BTN_STATE_CHORD_WAIT;
                    g_ButtonContext.first_chord_btn = btn;
                } else if (btn != BTN_NONE) {
                    g_ButtonContext.state = BTN_STATE_SINGLE_PRESSED;
                    g_ButtonContext.pressed_button = btn;
                    g_ButtonContext.long_press_fired = false;
                }
            }
            break;

        case BTN_STATE_SINGLE_PRESSED:
            event = HandleSinglePress(any_pressed);
            break;

        case BTN_STATE_CHORD_WAIT:
            event = HandleChordWait(any_pressed, gpio);
            break;

        case BTN_STATE_CHORD_ACTIVE:
            if (!any_pressed) {
                if (!g_ButtonContext.chord_long_fired) event = BTN_EVENT_CHORD_SHORT;
                g_ButtonContext.state = BTN_STATE_IDLE;
            } else if (g_ButtonContext.buttons_tick >= LONG_PRESS_TICKS && !g_ButtonContext.chord_long_fired) {
                event = BTN_EVENT_CHORD_LONG;
                g_ButtonContext.chord_long_fired = true;
            }
            break;

        case BTN_STATE_IGNORED:
            if (!any_pressed) g_ButtonContext.state = BTN_STATE_IDLE;
            break;

        default:
            g_ButtonContext.state = BTN_STATE_IDLE;
            break;
    }
    return event;
}

/**
 * @brief Сброс состояний (при ошибках или инициализации)
 */
void BUTTONS_ResetFSM(void) {
    g_ButtonContext.state = BTN_STATE_IDLE;
    g_ButtonContext.buttons_tick = 0;
    g_ButtonContext.long_press_fired = false;
}

uint16_t BUTTONS_GetRepeatIteration(void) {
    // Возвращаем количество интервалов удержания для нелинейного изменения значений
    if (!g_ButtonContext.long_press_fired) return 0;
    return (g_ButtonContext.buttons_tick / REPEAT_INTERVAL_TICKS);
}
