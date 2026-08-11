/**
 * @file commands.c
 * @brief Обработка команд и кнопок. Исправлена логика SET2 для пресетов.
 */

#include "commands.h"
#include "state.h"
#include "config.h"
#include "heater.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ========================================================================== */
/* Вспомогательные структуры                                                  */
/* ========================================================================== */

typedef struct {
    uint16_t *p_val;
    uint16_t min;
    uint16_t max;
    uint16_t step;
    void (*setter)(uint16_t);
} EditContext_t;

/* ========================================================================== */
/* Локальные функции                                                          */
/* ========================================================================== */

static int16_t CalculateStep(uint16_t base_step, bool is_up) {
    uint8_t iter = BUTTONS_GetRepeatIteration();
    int16_t s = base_step;
    if (iter >= 15)      s *= 10;
    else if (iter >= 5)  s *= 5;
    return is_up ? s : -s;
}

static bool GetEditContext(EditContext_t *ctx) {
    SystemMode_t mode = STATE_GetMode();

    // 1. Рабочий экран (Температура)
    if (mode == SYS_MODE_MAIN_DESOLDER || mode == SYS_MODE_MAIN_SOLDER) {
        bool is_solder = (mode == SYS_MODE_MAIN_SOLDER);
        ctx->p_val  = is_solder ? &g_TempSettings.targetSetSolder : &g_TempSettings.targetSetDesolder;
        ctx->setter = is_solder ? CONFIG_SetTargetSolder : CONFIG_SetTargetDesolder;
        ctx->min = SET_MIN; ctx->max = SET_MAX; ctx->step = 1;
        return true;
    }

    // 2. Сервисное меню — min/max/step берутся из самой таблицы
    // g_ServiceMenu (ServiceMenuItem_t), а не переопределяются здесь
    // по строковому сравнению лейбла. Раньше эти хардкоды (0-240,
    // 50-450) вообще не совпадали с реальными пределами, которые
    // и так уже клэмпит сам сеттер (TIMEOUT_MIN/MAX, SLEEP_TEMP_MIN/MAX) —
    // теперь единственный источник истины — сама таблица.
    if (mode == SYS_MODE_SERVICE || mode == SYS_MODE_EXPERT) {
        const ServiceMenuItem_t *item = STATE_GetServiceMenuItem(STATE_GetMenuCursor());
        if (!item) return false;

        bool is_solder = STATE_IsServiceEditingSolder();
        ctx->p_val  = is_solder ? item->valueSolder : item->valueDesolder;
        ctx->setter = is_solder ? item->setterSolder : item->setterDesolder;
        ctx->min = item->min; ctx->max = item->max; ctx->step = item->step ? item->step : 1;
        return true;
    }
    return false;
}

static void ChangeValue(int8_t dir, bool use_accel) {
    EditContext_t ctx;
    if (!GetEditContext(&ctx)) return;

    int16_t delta = use_accel ? CalculateStep(ctx.step, dir > 0) : (dir > 0 ? (int16_t)ctx.step : -(int16_t)ctx.step);

    int32_t newVal = (int32_t)(*ctx.p_val) + delta;

    /* При ускорении (шаг умножен на 5 или 10, см. CalculateStep) не
       просто прибавляем шаг от текущего значения, а "перепрыгиваем"
       на ближайшее круглое число, кратное этому шагу, в направлении
       движения — иначе на практике получаются некруглые значения
       вроде 173→178→183 вместо ожидаемого 173→175→180.
       Раньше это не применялось к таймерам (PreSleep/Standby) из
       расчёта "они хранятся в тиках, округление даёт некруглые
       минуты" — это было неверно: они хранятся в минутах напрямую
       (см. CONFIG_MinutesToTicks), так что округление тут так же
       уместно, как и везде. */
    if (use_accel) {
        int16_t roundStep = (delta < 0) ? -delta : delta;
        if (roundStep >= 5) {
            int32_t cur = (int32_t)(*ctx.p_val);
            if (dir > 0) {
                newVal = ((cur / roundStep) + 1) * roundStep;
            } else {
                newVal = (cur % roundStep == 0) ? (cur - roundStep)
                                                 : ((cur / roundStep) * roundStep);
            }
        }
    }

    // Ограничения
    if (newVal < ctx.min) newVal = ctx.min;
    if (newVal > ctx.max) newVal = ctx.max;

    // Запись
    if (ctx.setter) ctx.setter((uint16_t)newVal);
    else *ctx.p_val = (uint16_t)newVal;

    // Вызов callback-а обновления (например, для сохранения в EEPROM или применения частоты)
    if (STATE_GetMode() >= SYS_MODE_SERVICE) {
        const ServiceMenuItem_t *item = STATE_GetServiceMenuItem(STATE_GetMenuCursor());
        if (item && item->onChanged) item->onChanged();
    }
}

/* ========================================================================== */
/* Основная обработка кнопок                                                  */
/* ========================================================================== */

void COMMANDS_HandleButtonEvent(ButtonEvent_t event) {
    SystemMode_t mode = STATE_GetMode();

    /* ------------------------------------------------------------------ */
    /* Экран предупреждения Expert: SET2_LONG = войти, всё остальное = выйти */
    /* ------------------------------------------------------------------ */
    if (mode == SYS_MODE_EXPERT_WARN) {
        if (event == BTN_EVENT_SET2_LONG) {
            STATE_EnterExpert();
        } else {
            STATE_SetMode(SYS_MODE_SERVICE);
            STATE_ResetMenu();
        }
        return;
    }

    /* ------------------------------------------------------------------ */
    /* Expert меню                                                          */
    /* ------------------------------------------------------------------ */
    if (mode == SYS_MODE_EXPERT) {
        switch (event) {
            case BTN_EVENT_UP_SHORT:
                STATE_ExpertMenuNavigate(-1);
                break;
            case BTN_EVENT_DN_SHORT:
                STATE_ExpertMenuNavigate(1);
                break;
            case BTN_EVENT_UP_REPEAT:
                /* CalculateStep уже возвращает знаковый шаг: is_up=true → +s */
                STATE_ExpertMenuNavigateAccel(CalculateStep(1, true));
                break;
            case BTN_EVENT_DN_REPEAT:
                STATE_ExpertMenuNavigateAccel(CalculateStep(1, false));
                break;

            case BTN_EVENT_SET2_SHORT:
                /* На "Сброс" — выполнить сброс; на "Выход" — выйти; иначе — toggle edit */
            {
                uint8_t cur = STATE_GetExpertCursor();
                const char *lbl = STATE_GetExpertItemLabel(cur);
                if (strcmp(lbl, "Выход") == 0) {
                    STATE_ExitExpert();
                } else if (strcmp(lbl, "Сброс") == 0) {
                    STATE_ExpertDoReset();
                } else {
                    STATE_ExpertMenuToggleEdit();
                }
            }
                break;

            case BTN_EVENT_SET2_LONG:
                /* Длинное SET2 тоже подтверждает действие на Сброс/Выход */
            {
                uint8_t cur = STATE_GetExpertCursor();
                const char *lbl = STATE_GetExpertItemLabel(cur);
                if (strcmp(lbl, "Выход") == 0) {
                    STATE_ExitExpert();
                } else if (strcmp(lbl, "Сброс") == 0) {
                    STATE_ExpertDoReset();
                }
            }
                break;

            case BTN_EVENT_TOOL_SHORT:
                STATE_ServiceToggleTool();
                break;

            case BTN_EVENT_CHORD_LONG:
                /* Аккорд из Expert — выход в рабочий экран (как из сервиса) */
                COMMANDS_ToggleServiceMode();
                break;

            default:
                break;
        }
        return;
    }

    /* ------------------------------------------------------------------ */
    /* Сервисное и рабочее меню (оригинальная логика)                      */
    /* ------------------------------------------------------------------ */
    bool is_service = (mode == SYS_MODE_SERVICE);
    bool is_editing = STATE_IsEditing();

    switch (event) {
        case BTN_EVENT_UP_SHORT:
        case BTN_EVENT_DN_SHORT: {
            int8_t dir = (event == BTN_EVENT_UP_SHORT) ? 1 : -1;
            if (is_service && !is_editing) STATE_MenuNavigate(-dir);
            else ChangeValue(dir, false);
        } break;

        case BTN_EVENT_UP_REPEAT:
        case BTN_EVENT_DN_REPEAT:
            ChangeValue((event == BTN_EVENT_UP_REPEAT) ? 1 : -1, true);
            break;

        case BTN_EVENT_SET2_SHORT:
            if (is_service) {
                /* На пункте "Выход" — выйти из сервисного меню */
                if (strcmp(STATE_GetItemLabel(STATE_GetMenuCursor()), "Выход") == 0) {
                    COMMANDS_ToggleServiceMode();
                } else {
                    STATE_MenuToggleEdit();
                }
            } else {
                COMMANDS_ApplyPreset(2);
            }
            break;

        case BTN_EVENT_SET2_LONG:
            if (is_service) {
                /* Длинное SET2 на "Expert" → экран предупреждения */
                if (strcmp(STATE_GetItemLabel(STATE_GetMenuCursor()), "Expert") == 0) {
                    STATE_EnterExpertWarn();
                }
                /* На "Выход" — выйти */
                else if (strcmp(STATE_GetItemLabel(STATE_GetMenuCursor()), "Выход") == 0) {
                    COMMANDS_ToggleServiceMode();
                }
                /* иначе — сохранить пресет (оригинальное поведение вне сервиса не применимо здесь) */
            } else {
                COMMANDS_SaveToPreset(2);
            }
            break;

        case BTN_EVENT_TOOL_SHORT:
            is_service ? STATE_ServiceToggleTool() : COMMANDS_ToggleTool();
            break;

        case BTN_EVENT_SET1_SHORT:
        case BTN_EVENT_SET3_SHORT:
            if (!is_service) {
                uint8_t num = (event == BTN_EVENT_SET1_SHORT) ? 1 : 3;
                COMMANDS_ApplyPreset(num);
            }
            break;

        case BTN_EVENT_SET1_LONG:
        case BTN_EVENT_SET3_LONG:
            if (!is_service) {
                uint8_t num = (event == BTN_EVENT_SET1_LONG) ? 1 : 3;
                COMMANDS_SaveToPreset(num);
            }
            break;

        case BTN_EVENT_CHORD_SHORT:
            if (!is_service) COMMANDS_TogglePower();
            break;

        case BTN_EVENT_CHORD_LONG:
            COMMANDS_ToggleServiceMode();
            break;

        default: break;
    }
}

/* ========================================================================== */
/* Реализация команд                                                          */
/* ========================================================================== */

void COMMANDS_ToggleTool(void) {
    bool want_solder = !g_WorkFlags.tool;
    if (!HEATER_IsToolOk(want_solder)) return; /* инструмент неисправен/не подключен */
    g_WorkFlags.tool = want_solder;
    STATE_SetMode(g_WorkFlags.tool ? SYS_MODE_MAIN_SOLDER : SYS_MODE_MAIN_DESOLDER);
}

void COMMANDS_TogglePower(void) {
    bool is_desolder = (STATE_GetMode() == SYS_MODE_MAIN_DESOLDER);
    volatile bool *pwr_flag = is_desolder ? &g_WorkFlags.pwrIsOnVac : &g_WorkFlags.pwrIsOnSolder;
    bool turning_on = !(*pwr_flag);

    if (turning_on && !HEATER_IsToolOk(!is_desolder)) return; /* неисправен/не подключен */

    if (turning_on) {
        /* Полный сброс: активный режим, сброс ПИД и ВСЕХ таймеров сна —
           тот же путь, что и при пробуждении из докстанции. */
        is_desolder ? HEATER_ResetSleepDesolder() : HEATER_ResetSleepSolder();
    } else {
        *pwr_flag = false;
        is_desolder ? STATE_DeactivateSleepDesolder() : STATE_DeactivateSleepSolder();
    }

    /* Персистентность статуса вкл/выкл — как и SET*, переживает перезагрузку */
    is_desolder ? CONFIG_SetToolEnabledDesolder(*pwr_flag) : CONFIG_SetToolEnabledSolder(*pwr_flag);
}

void COMMANDS_ToggleServiceMode(void) {
    if (STATE_GetMode() >= SYS_MODE_SERVICE) {
        STATE_SetMode(g_WorkFlags.tool ? SYS_MODE_MAIN_SOLDER : SYS_MODE_MAIN_DESOLDER);
    } else {
        STATE_SetMode(SYS_MODE_SERVICE);
        STATE_ResetMenu();
    }
}

void COMMANDS_ApplyPreset(uint8_t num) {
    if (num < 1 || num > 3) return;
    bool is_desolder = (STATE_GetMode() == SYS_MODE_MAIN_DESOLDER);
    uint16_t *base = is_desolder ? &g_TempSettings.preSet1Desolder : &g_TempSettings.preSet1Solder;
    uint16_t temp = *(base + (num - 1));
    is_desolder ? CONFIG_SetTargetDesolder(temp) : CONFIG_SetTargetSolder(temp);
}

void COMMANDS_SaveToPreset(uint8_t num) {
    if (num < 1 || num > 3) return;
    bool is_desolder = (STATE_GetMode() == SYS_MODE_MAIN_DESOLDER);
    uint16_t current_t = is_desolder ? g_tCurrentDesolder : g_tCurrentSolder;
    is_desolder ? CONFIG_SetPresetDesolder(num, current_t) : CONFIG_SetPresetSolder(num, current_t);
}
