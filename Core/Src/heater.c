/**
 * @file heater.c
 * @brief Управление нагревателями, ПИД-регулятор, программный ШИМ,
 *        двухступенчатые таймеры сна.
 *
 * Вызов: HEATER_Tick() из HAL_TIM_PeriodElapsedCallback (TIM3, 200 Гц).
 *
 * Логика сна (одинакова для паяльника и отсоса):
 *   Старт:  counter = preSleepTimeout × 2  (тики TIM10 по 30 сек)
 *   counter == 0 (1й раз): preSleep = true,  T → sleepTemp,
 *                           counter = sleepTimeout × 2
 *   counter == 0 (2й раз): pwrIsOn = false, нагреватель выключен
 *   Сброс:  preSleep = false, counter = preSleepTimeout × 2, T → targetSet
 *
 * Сброс паяльника — DOCK (EXTI → HEATER_ResetSleepSolder).
 * Сброс отсоса   — BTN_VAC передний фронт (main loop → HEATER_ResetSleepDesolder).
 *
 * ШИМ: программный, период PWM_PERIOD_TICKS тиков (1 сек @ 200 Гц).
 *   duty_ticks = (uint32_t)(pid.output × PWM_PERIOD_TICKS)
 *   pwm_counter < duty_ticks → HIGH, иначе LOW.
 *   Нагреватель НЕ включается если: !pwrIsOn, обрыв нагревателя.
 *
 * Контроль целостности: опрашивается пока ключ закрыт (pwm LOW фаза).
 *   PB1 (Solder_Test), PB2 (Desolder_Test): HIGH = исправно.
 */

#include "heater.h"
#include "config.h"
#include "main.h"
#include "tim.h"
#include <string.h>

/* =========================================================================
 * Внутренние типы
 * ========================================================================= */

/** Двухступенчатый автомат сна для одного инструмента */
typedef enum {
    SLEEP_STATE_ACTIVE = 0,  /* Нормальная работа */
    SLEEP_STATE_PRESLEEP,    /* Полусон: T = sleepTemp */
    SLEEP_STATE_OFF,         /* Выключен: нагреватель отключён */
} SleepState_t;

typedef struct {
    PID_t        pid;
    SleepState_t sleep_state;
    HeaterStatus_t status;
    uint32_t     pwm_counter;   /* Счётчик ШИМ 0..PWM_PERIOD_TICKS-1 */
    uint32_t     duty_ticks;    /* Количество тиков HIGH за период */
    uint32_t     pid_counter;   /* Счётчик для запуска ПИД */
    RtdFault_t   fault_candidate;  /* см. EvaluateRtdFault + дебаунс */
    uint16_t     fault_debounce;
} HeaterChannel_t;

/* =========================================================================
 * Статические переменные
 * ========================================================================= */

static HeaterChannel_t s_solder;
static HeaterChannel_t s_desolder;  /* ПИД/нагрев отсоса пока не реализован — см. Channel_Tick_Desolder */

/* =========================================================================
 * Публичные функции
 * ========================================================================= */

/* =========================================================================
 * Внутренние функции: ПИД
 * ========================================================================= */

static void PID_Init(PID_t *pid, float kp, float ki, float kd) {
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->integral      = 0.0f;
    pid->prev_error    = 0.0f;
    pid->output        = 0.0f;
    pid->integral_limit = 100.0f;   /* % */
}

static void PID_UpdateCoeffs(PID_t *pid, float kp, float ki, float kd) {
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

/**
 * @brief Один шаг ПИД. dt = PID_PERIOD_TICKS / HEATER_TIM_FREQ_HZ = 0.1 сек.
 * @param setpoint  Уставка, °С
 * @param measured  Измеренная температура, °С
 * @return output   0.0 .. 1.0
 */
static float PID_Compute(PID_t *pid, float setpoint, float measured) {
    const float dt = (float)PID_PERIOD_TICKS / (float)HEATER_TIM_FREQ_HZ;

    float error = setpoint - measured;

    /* Пропорциональная */
    float p = pid->kp * error;

    /* Интегральная с ограничением (anti-windup) */
    pid->integral += pid->ki * error * dt;
    if (pid->integral >  pid->integral_limit) pid->integral =  pid->integral_limit;
    if (pid->integral < -pid->integral_limit) pid->integral = -pid->integral_limit;

    /* Дифференциальная */
    float d = pid->kd * (error - pid->prev_error) / dt;
    pid->prev_error = error;

    float out = p + pid->integral + d;

    /* Нормировка: считаем что kp*100 = 100% при ошибке 100°С */
    out /= 100.0f;

    /* Clamp 0..1 */
    if (out < 0.0f) out = 0.0f;
    if (out > 1.0f) out = 1.0f;

    pid->output = out;
    return out;
}

static void PID_Reset(PID_t *pid) {
    pid->integral   = 0.0f;
    pid->prev_error = 0.0f;
    pid->output     = 0.0f;
}

/* =========================================================================
 * Внутренние функции: GPIO
 * ========================================================================= */

static inline void Solder_SetPin(bool on) {
    HAL_GPIO_WritePin(Solder_On_GPIO_Port, Solder_On_Pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static inline void Desolder_SetPin(bool on) {
    HAL_GPIO_WritePin(Desolder_On_GPIO_Port, Desolder_On_Pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static inline void Pump_SetPin(bool on) {
    HAL_GPIO_WritePin(Pump_On_GPIO_Port, Pump_On_Pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static inline bool Solder_TestPin(void) {
    return HAL_GPIO_ReadPin(Solder_Test_GPIO_Port, Solder_Test_Pin) == GPIO_PIN_SET;
}

static inline bool Desolder_TestPin(void) {
    return HAL_GPIO_ReadPin(Desolder_Test_GPIO_Port, Desolder_Test_Pin) == GPIO_PIN_SET;
}

static inline bool VacBtn_Pressed(void) {
    /* PB12 с подтяжкой вверх: нажата = LOW */
    return HAL_GPIO_ReadPin(Btn_Pump_GPIO_Port, Btn_Pump_Pin) == GPIO_PIN_RESET;
}

/* =========================================================================
 * Внутренние функции: таймеры сна
 * ========================================================================= */

/**
 * @brief Целевая температура с учётом состояния сна.
 */
static float GetTargetSolder(void) {
    if (s_solder.sleep_state == SLEEP_STATE_PRESLEEP)
        return (float)g_ServiceSettings.sleepTempSolder;
    return (float)g_TempSettings.targetSetSolder;
}

static float __attribute__((unused)) GetTargetDesolder(void) {
    if (s_desolder.sleep_state == SLEEP_STATE_PRESLEEP)
        return (float)g_ServiceSettings.sleepTempDesolder;
    return (float)g_TempSettings.targetSetDesolder;
}

/**
 * @brief Обработка TIM10-тиков сна. Вызывается из CONFIG_DecrementSleepCounters.
 *        Здесь переключаем состояние автомата.
 *
 * Реализовано как callback — вызывается из config.c после декремента.
 */
void HEATER_OnSleepTickSolder(void) {
    if (!g_WorkFlags.pwrIsOnSolder) return;
    if (!s_solder.status.is_ok) return; /* канал неисправен/не подключен — таймер сна не тикает */

    switch (s_solder.sleep_state) {
        case SLEEP_STATE_ACTIVE:
            /* Счётчик дошёл до 0 — переходим в полусон */
            s_solder.sleep_state = SLEEP_STATE_PRESLEEP;
            g_WorkFlags.preSleepSolder = true;
            PID_Reset(&s_solder.pid);
            /* Взвод второй ступени */
            CONFIG_ResetSleepCounterToSleep(true);
            break;

        case SLEEP_STATE_PRESLEEP:
            /* Вторая ступень — выключение */
            s_solder.sleep_state = SLEEP_STATE_OFF;
            g_WorkFlags.pwrIsOnSolder = false;
            g_WorkFlags.preSleepSolder = false;
            CONFIG_DeactivateSleepCounterSolder();
            Solder_SetPin(false);
            PID_Reset(&s_solder.pid);
            break;

        default:
            break;
    }
}

void HEATER_OnSleepTickDesolder(void) {
    if (!g_WorkFlags.pwrIsOnVac) return;
    if (!s_desolder.status.is_ok) return; /* канал неисправен/не подключен — таймер сна не тикает */

    switch (s_desolder.sleep_state) {
        case SLEEP_STATE_ACTIVE:
            s_desolder.sleep_state = SLEEP_STATE_PRESLEEP;
            g_WorkFlags.preSleepDesolder = true;
            PID_Reset(&s_desolder.pid);
            CONFIG_ResetSleepCounterToSleep(false);
            break;

        case SLEEP_STATE_PRESLEEP:
            s_desolder.sleep_state = SLEEP_STATE_OFF;
            g_WorkFlags.pwrIsOnVac = false;
            g_WorkFlags.preSleepDesolder = false;
            CONFIG_DeactivateSleepCounterDesolder();
            Desolder_SetPin(false);
            PID_Reset(&s_desolder.pid);
            break;

        default:
            break;
    }
}

/* =========================================================================
 * Внутренние функции: диагностика RTD/подключения
 * ========================================================================= */

/** Порог "нереальной" температуры — за пределами реальный RTD не читает */
#define RTD_FAULT_HIGH_C   500

/** Дебаунс смены статуса неисправности: 40 тиков @ 200 Гц = 200 мс.
    Защита от одиночного выброса АЦП/дребезга Test-пина. */
#define RTD_FAULT_DEBOUNCE_TICKS  40u

static RtdFault_t EvaluateRtdFault(uint16_t temp_c, bool heater_ok) {
    bool zero     = (temp_c == 0);
    bool too_high = (temp_c > RTD_FAULT_HIGH_C);

    if (!heater_ok) {
        /* Test = 0: цепь нагревателя разомкнута */
        if (too_high) return RTD_NOT_CONNECTED;      /* инструмент не подключен */
        if (zero)     return RTD_SHORT_HEATER_OPEN;  /* редко: КЗ RTD + обрыв нагревателя */
        return HEATER_OPEN;                          /* RTD в норме — оборван только нагреватель */
    }

    /* Test = 1: нагреватель цел, инструмент физически подключен */
    if (zero)     return RTD_SHORT;
    if (too_high) return RTD_OPEN;
    return RTD_OK;
}

/**
 * @brief Применяет дебаунс к новому вычисленному состоянию RTD и
 *        обновляет ch->status.rtd_fault/is_ok только после того, как
 *        состояние продержалось стабильным RTD_FAULT_DEBOUNCE_TICKS тиков.
 */
static void UpdateRtdFault(HeaterChannel_t *ch, uint16_t temp_c, bool heater_ok) {
    RtdFault_t candidate = EvaluateRtdFault(temp_c, heater_ok);

    if (candidate != ch->fault_candidate) {
        ch->fault_candidate = candidate;
        ch->fault_debounce  = 0;
    } else if (ch->fault_debounce < RTD_FAULT_DEBOUNCE_TICKS) {
        ch->fault_debounce++;
    }

    if (ch->fault_debounce >= RTD_FAULT_DEBOUNCE_TICKS) {
        ch->status.rtd_fault = candidate;
        ch->status.is_ok     = (candidate == RTD_OK);
    }
}

static void Channel_Tick_Solder(void) {
    bool enabled = g_WorkFlags.pwrIsOnSolder;

    /* --- Обновление коэффициентов ПИД из конфига --- */
    PID_UpdateCoeffs(&s_solder.pid,
        g_ServiceSettings.KpSolder / 100.0f,
        g_ServiceSettings.KiSolder / 100.0f,
        g_ServiceSettings.KdSolder / 100.0f);

    /* --- ПИД пересчёт каждые PID_PERIOD_TICKS --- */
    s_solder.pid_counter++;
    if (s_solder.pid_counter >= PID_PERIOD_TICKS) {
        s_solder.pid_counter = 0;

        if (enabled) {
            float target  = GetTargetSolder();
            float current = (float)g_tCurrentSolder;
            float out = PID_Compute(&s_solder.pid, target, current);
            s_solder.duty_ticks = (uint32_t)(out * (float)PWM_PERIOD_TICKS);
        } else {
            s_solder.duty_ticks = 0;
        }
    }

    /* --- Программный ШИМ --- */
    s_solder.pwm_counter++;
    if (s_solder.pwm_counter >= PWM_PERIOD_TICKS) {
        s_solder.pwm_counter = 0;
    }

    bool pwm_on = (s_solder.pwm_counter < s_solder.duty_ticks);

    /* --- Контроль целостности нагревателя (пока ключ закрыт) --- */
    if (!pwm_on) {
        s_solder.status.heater_ok = Solder_TestPin();
    }

    /* --- Диагностика RTD/подключения (ADS1220 + Test) --- */
    UpdateRtdFault(&s_solder, g_tCurrentSolder, s_solder.status.heater_ok);

    /* --- Управление ключом ---
       ВАЖНО: гейтим не только по heater_ok (цепь нагревателя цела),
       но и по status.is_ok — иначе при обрыве/КЗ RTD ПИД продолжит
       слепо греть по заведомо неверному показанию температуры. */
    bool drive = enabled && pwm_on && s_solder.status.is_ok;
    Solder_SetPin(drive);
    if (!s_solder.status.is_ok) {
        PID_Reset(&s_solder.pid); /* не копим интеграл, пока канал заблокирован */
    }

    /* --- Статус --- */
    s_solder.status.in_presleep = (s_solder.sleep_state == SLEEP_STATE_PRESLEEP);
    s_solder.status.duty = (float)s_solder.duty_ticks / (float)PWM_PERIOD_TICKS;
}

static void Channel_Tick_Desolder(void) {
    /* ПИД/нагрев отсоса пока не реализован — ключ всегда выключен.
       Диагностика RTD/подключения при этом уже полноценно рабочая,
       т.к. чтение ADS1220 для отсоса реализовано. */
    Desolder_SetPin(false);
    s_desolder.status.heater_ok = Desolder_TestPin();
    s_desolder.status.duty = 0.0f;

    UpdateRtdFault(&s_desolder, g_tCurrentDesolder, s_desolder.status.heater_ok);
}

/* =========================================================================
 * Публичные функции
 * ========================================================================= */

void HEATER_Init(void) {
    memset(&s_solder,   0, sizeof(s_solder));
    memset(&s_desolder, 0, sizeof(s_desolder));

    /* Инициализация ПИД паяльника */
    PID_Init(&s_solder.pid,
        g_ServiceSettings.KpSolder / 100.0f,
        g_ServiceSettings.KiSolder / 100.0f,
        g_ServiceSettings.KdSolder / 100.0f);

    /* Инициализация ПИД отсоса */
    PID_Init(&s_desolder.pid,
        g_ServiceSettings.KpDesolder / 100.0f,
        g_ServiceSettings.KiDesolder / 100.0f,
        g_ServiceSettings.KdDesolder / 100.0f);

    /* Состояние сна — активное, счётчики взведены в CONFIG_Init */
    s_solder.sleep_state   = SLEEP_STATE_ACTIVE;
    s_desolder.sleep_state = SLEEP_STATE_ACTIVE;

    /* Все выходы в 0 */
    Solder_SetPin(false);
    Desolder_SetPin(false);
    Pump_SetPin(false);

    /* Запуск TIM3 */
    HAL_TIM_Base_Start_IT(&htim3);
}

void HEATER_Tick(void) {
    Channel_Tick_Solder();
    Channel_Tick_Desolder();
}

HeaterStatus_t HEATER_GetStatusSolder(void) {
    return s_solder.status;
}

HeaterStatus_t HEATER_GetStatusDesolder(void) {
    return s_desolder.status;
}

bool HEATER_IsToolOk(bool solder) {
    return solder ? s_solder.status.is_ok : s_desolder.status.is_ok;
}

void HEATER_ResetSleepSolder(void) {
    s_solder.sleep_state        = SLEEP_STATE_ACTIVE;
    g_WorkFlags.preSleepSolder  = false;
    /* Если был выключен — включаем обратно */
    if (!g_WorkFlags.pwrIsOnSolder) {
        g_WorkFlags.pwrIsOnSolder = true;
    }
    PID_Reset(&s_solder.pid);
    CONFIG_ResetSleepCounterSolder();
    CONFIG_ActivateSleepCounterSolder();
}

void HEATER_ResetSleepDesolder(void) {
    s_desolder.sleep_state        = SLEEP_STATE_ACTIVE;
    g_WorkFlags.preSleepDesolder  = false;
    if (!g_WorkFlags.pwrIsOnVac) {
        g_WorkFlags.pwrIsOnVac = true;
    }
    PID_Reset(&s_desolder.pid);
    CONFIG_ResetSleepCounterDesolder();
    CONFIG_ActivateSleepCounterDesolder();
}
