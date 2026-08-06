/**
 * @file heater.h
 * @brief Управление нагревателями и ПИД-регулятор.
 *
 * Архитектура:
 *  - TIM3 @ 200 Гц → HEATER_Tick() из прерывания
 *  - ПИД пересчитывается каждые PID_PERIOD_TICKS тиков (100 мс)
 *  - Программный ШИМ: период PWM_PERIOD_TICKS тиков (1 сек),
 *    duty = PID output (0..PWM_PERIOD_TICKS)
 *  - Нагреватели: PB4 (паяльник), PB5 (отсос) — активный высокий
 *  - Насос:       PB13 — активный высокий, управляется BTN_VAC (PB12)
 *  - Контроль целостности: PB1 (паяльник), PB2 (отсос) — высокий = исправно
 */

#ifndef HEATER_H
#define HEATER_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * Константы
 * ========================================================================= */

/** Частота TIM3, Гц */
#define HEATER_TIM_FREQ_HZ      200u

/** Период ПИД-цикла: каждые 20 тиков = 100 мс */
#define PID_PERIOD_TICKS        20u

/** Период программного ШИМ: 200 тиков = 1 сек */
#define PWM_PERIOD_TICKS        200u

/* =========================================================================
 * Типы
 * ========================================================================= */

typedef struct {
    float kp, ki, kd;
    float integral;
    float prev_error;
    float output;           /* 0.0 .. 1.0 */
    float integral_limit;   /* Ограничение интегральной составляющей */
} PID_t;

typedef struct {
    bool  heater_ok;        /* false = обрыв/КЗ нагревателя */
    bool  in_presleep;      /* прибор в полусне */
    float duty;             /* текущий duty цикл 0.0..1.0 */
} HeaterStatus_t;

/* =========================================================================
 * API
 * ========================================================================= */

/**
 * @brief Инициализация модуля. Вызвать один раз после CONFIG_Init().
 *        Запускает TIM3.
 */
void HEATER_Init(void);

/**
 * @brief Тик 200 Гц — вызывается из HAL_TIM_PeriodElapsedCallback (TIM3).
 *        Содержит ПИД, программный ШИМ, контроль целостности, насос.
 */
void HEATER_Tick(void);

/**
 * @brief Статус нагревателя паяльника.
 */
HeaterStatus_t HEATER_GetStatusSolder(void);

/**
 * @brief Статус нагревателя отсоса.
 */
HeaterStatus_t HEATER_GetStatusDesolder(void);

/**
 * @brief Сброс таймера сна паяльника (вызывается из DOCK EXTI).
 */
void HEATER_ResetSleepSolder(void);

/**
 * @brief Сброс таймера сна отсоса (вызывается при нажатии BTN_VAC).
 */
void HEATER_ResetSleepDesolder(void);

#endif /* HEATER_H */
