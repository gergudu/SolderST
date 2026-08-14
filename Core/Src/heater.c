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
#include "buzzer.h"
#include <string.h>
#include <math.h>

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
static HeaterChannel_t s_desolder;

/* Текущее состояние силового питания (PWR_ON, PC13, push-pull,
   HIGH = включено). См. HEATER_UpdatePowerPin(). */
static bool s_powered = false;

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
 * @brief Пересчёт ПИД-регулятора. Вызывается каждые PID_PERIOD_TICKS (0.1 сек).
 * @param setpoint  Целевая температура, °C
 * @param measured  Измеренная температура с RTD, °C
 * @return output   ШИМ-выход от 0.0 (0%) до 1.0 (100%)
 */
static float PID_Compute(PID_t *pid, float setpoint, float measured) {
    const float dt = (float)PID_PERIOD_TICKS / (float)HEATER_TIM_FREQ_HZ; /* 0.1 сек */

    float error = setpoint - measured;

    /* 1. Пропорциональная составляющая */
    float p = (pid->kp / 100.0f) * error;

    /* 2. Дифференциальная составляющая по ИЗМЕРЕНИЮ с простым фильтром */
        static float d_filtered = 0.0f;
        float d_raw = 0.0f;

        if (pid->prev_error != 0.0f) {
            float d_measured = measured - pid->prev_error;
            d_raw = -(pid->kd / 100.0f) * (d_measured / dt);
        }
        pid->prev_error = measured;

        /* Фильтр низкой частоты (EMA) для сглаживания скачков целых градусов */
        d_filtered = d_filtered * 0.6f + d_raw * 0.4f;
        float d = d_filtered;

    /* 3. Предварительный расчёт без I-составляющей */
    float out_unclamped = p + d;

    /* 4. Интегратор с Anti-Windup и зоной нечувствительности (+/- 3 °C) */
    /* Накопление разрешено только если выход не засыщен и мы близко к уставке */
    if (out_unclamped < 1.0f && out_unclamped > 0.0f && fabsf(error) <= 3.0f) {
        pid->integral += (pid->ki / 100.0f) * error * dt;
    }

    /* Жёсткий лимит самого интегратора (не более 30% мощности) */
    if (pid->integral > 0.3f)  pid->integral = 0.3f;
    if (pid->integral < 0.0f)  pid->integral = 0.0f;

    /* 5. Суммарный выход */
    float out = out_unclamped + pid->integral;

    /* 6. Ограничение ШИМ от 0.0 до 1.0 */
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

/** Указатели на GPIO-функции конкретного канала — параметризуют
    Channel_Tick()/HEATER_OnSleepTick(), чтобы не дублировать логику
    паяльника/отсоса. */
typedef void (*SetPinFn)(bool on);
typedef bool (*TestPinFn)(void);

/* =========================================================================
 * Внутренние функции: таймеры сна
 * ========================================================================= */

/**
 * @brief Целевая температура с учётом состояния сна.
 *        Публичная — единая точка истины для "эффективной" уставки
 *        (см. пояснение в heater.h и config.h).
 */
float HEATER_GetEffectiveTargetSolder(void) {
    if (s_solder.sleep_state == SLEEP_STATE_PRESLEEP)
        return (float)g_ServiceSettings.sleepTempSolder;
    return (float)g_TempSettings.targetSetSolder;
}

float HEATER_GetEffectiveTargetDesolder(void) {
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
/**
 * @brief Обработка TIM10-тиков сна для одного канала — единая логика
 *        автомата (была продублирована в HEATER_OnSleepTickSolder/
 *        Desolder ~25 идентичных строк на канал).
 * @param ch            Канал (s_solder/s_desolder)
 * @param is_solder      Для выбора CONFIG_ResetSleepCounterToSleep()/
 *                       CONFIG_Deactivate*Counter{Solder,Desolder}()
 * @param pwrIsOn        &g_WorkFlags.pwrIsOnSolder / pwrIsOnVac
 * @param preSleepFlag   &g_WorkFlags.preSleepSolder / preSleepDesolder
 * @param set_pin        Solder_SetPin / Desolder_SetPin
 */
static void HEATER_OnSleepTick(HeaterChannel_t *ch, bool is_solder,
                                volatile bool *pwrIsOn, volatile bool *preSleepFlag,
                                SetPinFn set_pin) {
    if (!*pwrIsOn) return;
    if (!ch->status.is_ok) return; /* канал неисправен/не подключен — таймер сна не тикает */

    switch (ch->sleep_state) {
        case SLEEP_STATE_ACTIVE:
            /* Счётчик дошёл до 0 — переходим в полусон */
            ch->sleep_state = SLEEP_STATE_PRESLEEP;
            *preSleepFlag = true;
            PID_Reset(&ch->pid);
            /* Взвод второй ступени */
            CONFIG_ResetSleepCounterToSleep(is_solder);
            BUZZER_Beep(250, BUZZER_PRIO_PRESLEEP); /* преслип — один короткий */
            break;

        case SLEEP_STATE_PRESLEEP:
            /* Вторая ступень — выключение */
            ch->sleep_state = SLEEP_STATE_OFF;
            *pwrIsOn = false;
            *preSleepFlag = false;
            if (is_solder) CONFIG_DeactivateSleepCounterSolder();
            else            CONFIG_DeactivateSleepCounterDesolder();
            set_pin(false);
            PID_Reset(&ch->pid);
            BUZZER_Beep(1000, BUZZER_PRIO_SLEEP); /* полный сон — один длинный */
            break;

        default:
            break;
    }
}

void HEATER_OnSleepTickSolder(void) {
    HEATER_OnSleepTick(&s_solder, true,
                        &g_WorkFlags.pwrIsOnSolder, &g_WorkFlags.preSleepSolder,
                        Solder_SetPin);
}

void HEATER_OnSleepTickDesolder(void) {
    HEATER_OnSleepTick(&s_desolder, false,
                        &g_WorkFlags.pwrIsOnVac, &g_WorkFlags.preSleepDesolder,
                        Desolder_SetPin);
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

/**
 * @brief Тик одного канала нагрева (ПИД + программный ШИМ + гейтинг
 *        по целостности) — была продублирована в Channel_Tick_Solder/
 *        Desolder, ~50 идентичных строк на канал.
 * @param ch             Канал (s_solder/s_desolder)
 * @param enabled         g_WorkFlags.pwrIsOnSolder / pwrIsOnVac (значение,
 *                        не указатель — здесь только читаем)
 * @param kp,ki,kd        Коэффициенты ПИД из g_ServiceSettings, уже
 *                        поделенные на 100 (вызывающая сторона)
 * @param target          HEATER_GetEffectiveTargetSolder/Desolder()
 * @param current_temp    g_tCurrentSolder / g_tCurrentDesolder
 * @param set_pin         Solder_SetPin / Desolder_SetPin
 * @param test_pin        Solder_TestPin / Desolder_TestPin
 */
static void Channel_Tick(HeaterChannel_t *ch, bool enabled,
                          float kp, float ki, float kd,
                          float target, uint16_t current_temp,
                          SetPinFn set_pin, TestPinFn test_pin) {
    /* --- Обновление коэффициентов ПИД из конфига --- */
    PID_UpdateCoeffs(&ch->pid, kp, ki, kd);

    /* --- ПИД пересчёт каждые PID_PERIOD_TICKS --- */
    ch->pid_counter++;
    if (ch->pid_counter >= PID_PERIOD_TICKS) {
        ch->pid_counter = 0;

        if (enabled) {
            float out = PID_Compute(&ch->pid, target, (float)current_temp);
            ch->duty_ticks = (uint32_t)(out * (float)PWM_PERIOD_TICKS);
        } else {
            ch->duty_ticks = 0;
        }
    }

    /* --- Программный ШИМ --- */
    ch->pwm_counter++;
    if (ch->pwm_counter >= PWM_PERIOD_TICKS) {
        ch->pwm_counter = 0;
    }

    bool pwm_on = (ch->pwm_counter < ch->duty_ticks);

    /* --- Контроль целостности нагревателя + RTD-диагностика ---
       Только при включённом силовом питании (PWR_ON/PC13) — при
       выключенном питании Test-пин читает обесточенную цепь и даёт
       ложный обрыв/КЗ. Пока не запитано, статус просто замораживается
       на последнем известном значении (безопасно: enabled уже false
       в это время, drive ниже и так не включит нагреватель). */
    if (s_powered) {
        if (!pwm_on) {
            ch->status.heater_ok = test_pin();
        }

        bool was_ok = ch->status.is_ok;
        UpdateRtdFault(ch, current_temp, ch->status.heater_ok);
        if (was_ok && !ch->status.is_ok) {
            BUZZER_BeepPattern(5, 250, 250, BUZZER_PRIO_FAULT); /* неисправность — 5 коротких */
        }
    }

    /* --- Управление ключом ---
       ВАЖНО: гейтим не только по heater_ok (цепь нагревателя цела),
       но и по status.is_ok — иначе при обрыве/КЗ RTD ПИД продолжит
       слепо греть по заведомо неверному показанию температуры. */
    bool drive = enabled && pwm_on && ch->status.is_ok;
    set_pin(drive);
    if (!ch->status.is_ok) {
        PID_Reset(&ch->pid); /* не копим интеграл, пока канал заблокирован */
    }

    /* --- Статус --- */
    ch->status.in_presleep = (ch->sleep_state == SLEEP_STATE_PRESLEEP);
    ch->status.duty = (float)ch->duty_ticks / (float)PWM_PERIOD_TICKS;
}

static void Channel_Tick_Solder(void) {
    Channel_Tick(&s_solder, g_WorkFlags.pwrIsOnSolder,
                 g_ServiceSettings.KpSolder / 100.0f,
                 g_ServiceSettings.KiSolder / 100.0f,
                 g_ServiceSettings.KdSolder / 100.0f,
                 HEATER_GetEffectiveTargetSolder(), g_tCurrentSolder,
                 Solder_SetPin, Solder_TestPin);
}

static void Channel_Tick_Desolder(void) {
    Channel_Tick(&s_desolder, g_WorkFlags.pwrIsOnVac,
                 g_ServiceSettings.KpDesolder / 100.0f,
                 g_ServiceSettings.KiDesolder / 100.0f,
                 g_ServiceSettings.KdDesolder / 100.0f,
                 HEATER_GetEffectiveTargetDesolder(), g_tCurrentDesolder,
                 Desolder_SetPin, Desolder_TestPin);
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

/**
 * @brief Управление силовым питанием (PWR_ON, PC13, push-pull,
 *        HIGH = включено). Включено, пока активен хотя бы один
 *        инструмент (pwrIsOnSolder || pwrIsOnVac) — например, один не
 *        подключен, а у второго ещё не сработал полный сон; или оба
 *        уже уснули (оба pwrIsOn == false) — тогда питание снимается.
 *        На переходе HIGH→LOW — сигнал отключения (один длинный, 3с).
 */
static void HEATER_UpdatePowerPin(void) {
    bool should_be_on = g_WorkFlags.pwrIsOnSolder || g_WorkFlags.pwrIsOnVac;
    if (should_be_on == s_powered) return;

    HAL_GPIO_WritePin(PWR_ON_GPIO_Port, PWR_ON_Pin, should_be_on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    if (!should_be_on) {
        BUZZER_Beep(3000, BUZZER_PRIO_POWEROFF); /* отключение питания */
    }
    s_powered = should_be_on;
}

void HEATER_Tick(void) {
    HEATER_UpdatePowerPin();
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

/**
 * @brief Тик насоса — единственная точка управления PB13/PB12
 *        (раньше дублировалась инлайном в main.c). Если отсос выключен
 *        (pwrIsOnVac == false) — кнопка неактивна.
 */
void HEATER_PumpTick(void) {
    static bool vac_prev = false;
    bool vac = VacBtn_Pressed() && g_WorkFlags.pwrIsOnVac;
    if (vac && !vac_prev) {
        HEATER_ResetSleepDesolder();
    }
    vac_prev = vac;
    Pump_SetPin(vac);
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
