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

/**
 * @brief Состояние RTD-датчика/нагревателя/подключения инструмента.
 *
 * Определяется по комбинации показания ADS1220 (температура после
 * пересчёта, °C) и состояния Solder_Test/Desolder_Test (нагреватель
 * должен быть выключен в момент опроса Test — см. Channel_Tick_*):
 *
 *   ADS > 500,        Test = 0  → RTD_NOT_CONNECTED       (инструмент не подключен)
 *   ADS == 0,         Test = 0  → RTD_SHORT_HEATER_OPEN   (редко: КЗ RTD + обрыв нагревателя одновременно)
 *   ADS == 0,         Test = 1  → RTD_SHORT               (RTD в коротком)
 *   ADS > 500,        Test = 1  → RTD_OPEN                (обрыв RTD)
 *   0 < ADS <= 500,   Test = 0  → HEATER_OPEN              (RTD исправен, оборван только нагреватель)
 *   0 < ADS <= 500,   Test = 1  → RTD_OK
 */
typedef enum {
    RTD_OK = 0,
    RTD_NOT_CONNECTED,
    RTD_SHORT_HEATER_OPEN,
    RTD_SHORT,
    RTD_OPEN,
    HEATER_OPEN,
} RtdFault_t;

typedef struct {
    bool       heater_ok;     /* false = обрыв/КЗ нагревателя (Test pin) */
    bool       in_presleep;   /* прибор в полусне */
    float      duty;          /* текущий duty цикл 0.0..1.0 */
    RtdFault_t rtd_fault;     /* см. RtdFault_t */
    bool       is_ok;         /* true только при rtd_fault == RTD_OK;
                                  разрешает ПИД, переключение фокуса Tools
                                  и таймеры сна для этого канала */
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
 * @brief Разрешена ли работа с каналом (RTD_OK): переключение фокуса
 *        Tools, ПИД, таймеры сна. false — инструмент не подключен
 *        или неисправен (RTD в коротком/обрыве).
 * @param solder true — паяльник, false — отсос
 */
bool HEATER_IsToolOk(bool solder);

/**
 * @brief Эффективная целевая температура — с учётом текущей ступени
 *        сна (полусон → sleepTemp вместо targetSet). Единая точка
 *        истины: только heater.c владеет автоматом сна, поэтому
 *        только здесь эту величину можно посчитать корректно.
 */
float HEATER_GetEffectiveTargetSolder(void);
float HEATER_GetEffectiveTargetDesolder(void);

/**
 * @brief Тик насоса — читает кнопку BTN_VAC, управляет реле насоса,
 *        сбрасывает таймер сна отсоса по переднему фронту. Вызывается
 *        из главного цикла main.c (не из ISR HEATER_Tick, чтобы не
 *        завязывать опрос кнопки на частоту TIM3).
 */
void HEATER_PumpTick(void);

/**
 * @brief Сброс таймера сна паяльника (вызывается из DOCK EXTI).
 */
void HEATER_ResetSleepSolder(void);

/**
 * @brief Сброс таймера сна отсоса (вызывается при нажатии BTN_VAC).
 */
void HEATER_ResetSleepDesolder(void);

#endif /* HEATER_H */
