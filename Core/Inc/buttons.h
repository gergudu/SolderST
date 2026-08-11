#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h"

/* =======================================================================
 КОНСТАНТЫ
 ======================================================================= */
#define LONG_PRESS_TICKS        100
#define REPEAT_INTERVAL_TICKS   10
#define CHORD_WINDOW_TICKS      40
#define FAST_STEP_ITER          10

/* ПИНЫ КНОПОК */
#define BTN_SET1_PIN    GPIO_PIN_8
#define BTN_SET2_PIN    GPIO_PIN_9
#define BTN_SET3_PIN    GPIO_PIN_10
#define BTN_DN_PIN      GPIO_PIN_11
#define BTN_UP_PIN      GPIO_PIN_12
#define BTN_TOOL_PIN    GPIO_PIN_15

#define BTN_ALL_PINS    (BTN_SET1_PIN | BTN_SET2_PIN | BTN_SET3_PIN | \
                         BTN_DN_PIN | BTN_UP_PIN | BTN_TOOL_PIN)

/* =======================================================================
 ПЕРЕЧИСЛЕНИЯ (Порядок критически важен!)
 ======================================================================= */
typedef enum {
	BTN_NONE = 0, BTN_SET1, BTN_SET2, BTN_SET3, BTN_DN, BTN_UP, BTN_TOOL
} Button_t;

typedef enum {
	BTN_EVENT_NONE = 0,

	/* Блок SHORT: индексы 1, 2, 3, 4, 5, 6 */
	BTN_EVENT_SET1_SHORT = 1,
	BTN_EVENT_SET2_SHORT,
	BTN_EVENT_SET3_SHORT,
	BTN_EVENT_DN_SHORT,
	BTN_EVENT_UP_SHORT,
	BTN_EVENT_TOOL_SHORT,

	/* Блок LONG: индексы 7, 8, 9, 10, 11, 12 */
	BTN_EVENT_SET1_LONG = 7,
	BTN_EVENT_SET2_LONG,
	BTN_EVENT_SET3_LONG,
	BTN_EVENT_DN_REPEAT,
	BTN_EVENT_UP_REPEAT,
	BTN_EVENT_TOOL_LONG,

	/* События вне арифметики */
	BTN_EVENT_CHORD_SHORT,
	BTN_EVENT_CHORD_LONG,
	BTN_EVENT_IGNORED
} ButtonEvent_t;

typedef enum {
	BTN_STATE_IDLE = 0,
	BTN_STATE_SINGLE_PRESSED,
	BTN_STATE_CHORD_WAIT,
	BTN_STATE_CHORD_ACTIVE,
	BTN_STATE_IGNORED
} ButtonState_t;

/* =======================================================================
 СТРУКТУРЫ И API
 ======================================================================= */
typedef struct {
	ButtonState_t state;
	Button_t pressed_button;
	Button_t first_chord_btn;
	uint16_t stable_mask;
	uint16_t buttons_tick;
	uint16_t chord_window;
	uint16_t repeat_counter; /* buttons_tick последнего сработавшего REPEAT —
	                             НЕ счётчик вызовов BUTTONS_Process(). Та
	                             вызывается из главного цикла и может идти
	                             медленнее ISR TIM5 (см. buttons.c), поэтому
	                             сравниваем с точным ISR-тиком buttons_tick,
	                             а не считаем сами вызовы. */
	uint16_t repeat_iteration;
	bool long_press_fired;
	bool chord_long_fired;
} ButtonContext_t;

extern volatile ButtonContext_t g_ButtonContext;

ButtonEvent_t BUTTONS_Process(void);
uint16_t BUTTONS_GetRepeatIteration(void);

#endif
