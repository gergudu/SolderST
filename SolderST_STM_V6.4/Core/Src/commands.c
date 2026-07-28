/**
 * @file commands.c
 * @brief Обработка команд и кнопок. Исправлена логика SET2 для пресетов.
 */

#include "commands.h"
#include "state.h"
#include "config.h"
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
    if (s > 10) s = 10;
    return is_up ? s : -s;
}

static bool GetEditContext(EditContext_t *ctx) {
    SystemMode_t mode = STATE_GetMode();

    // 1. Рабочий экран (Температура)
    if (mode == SYS_MODE_MAIN_DESOLDER || mode == SYS_MODE_MAIN_SOLDER) {
        bool is_solder = (mode == SYS_MODE_MAIN_SOLDER);
        ctx->p_val  = is_solder ? &g_TempSettings.targetSetSolder : &g_TempSettings.targetSetDesolder;
        ctx->setter = is_solder ? CONFIG_SetTargetSolder : CONFIG_SetTargetDesolder;
        ctx->min    = 50; ctx->max = 450;
        return true;
    }

    // 2. Сервисное меню
    if (mode == SYS_MODE_SERVICE || mode == SYS_MODE_EXPERT) {
        const ServiceMenuItem_t *item = STATE_GetServiceMenuItem(STATE_GetMenuCursor());
        if (!item) return false;

        bool is_solder = STATE_IsServiceEditingSolder();
        ctx->p_val  = is_solder ? item->valueSolder : item->valueDesolder;
        ctx->setter = is_solder ? item->setterSolder : item->setterDesolder;

        // --- ГИБКИЕ ГРАНИЦЫ ---
        const char* label = item->label; // Предполагаем, что в структуре есть label
        if (strcmp(label, "PreSleep") == 0 || strcmp(label, "Standby") == 0) {
            ctx->min = 0;   // Минимально 0 минут
            ctx->max = 240; // Максимально 120 минут (240 тиков)
        } else {
            ctx->min = 50;  // Для калибровки и прочего
            ctx->max = 450;
        }
        return true;
    }
    return false;
}

static void ChangeValue(int8_t dir, bool use_accel) {
    EditContext_t ctx;
    if (!GetEditContext(&ctx)) return;

    // Определяем, является ли параметр таймером (PreSleep/Standby)
    bool is_timer = false;
    if (STATE_GetMode() >= SYS_MODE_SERVICE) {
        const char* label = STATE_GetItemLabel(STATE_GetMenuCursor());
        if (strcmp(label, "PreSleep") == 0 || strcmp(label, "Standby") == 0) {
            is_timer = true;
        }
    }

    // Рассчитываем дельту
    int16_t delta = use_accel ? CalculateStep(1, dir > 0) : (dir > 0 ? 1 : -1);

    // Если это таймер, то на экране мы видим минуты, а в памяти лежат тики (1 мин = 2 тика)
    // Чтобы изменить на 1 минуту, нужно изменить на 2 тика.
    if (is_timer) {
        delta *= 2;
    }

    int32_t newVal = (int32_t)(*ctx.p_val) + delta;

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
    g_WorkFlags.tool = !g_WorkFlags.tool;
    STATE_SetMode(g_WorkFlags.tool ? SYS_MODE_MAIN_SOLDER : SYS_MODE_MAIN_DESOLDER);
}

void COMMANDS_TogglePower(void) {
    bool is_desolder = (STATE_GetMode() == SYS_MODE_MAIN_DESOLDER);
    volatile bool *pwr_flag = is_desolder ? &g_WorkFlags.pwrIsOnVac : &g_WorkFlags.pwrIsOnSolder;
    *pwr_flag = !(*pwr_flag);
    if (is_desolder) {
        *pwr_flag ? STATE_ActivateSleepDesolder() : STATE_DeactivateSleepDesolder();
    } else {
        *pwr_flag ? STATE_ActivateSleepSolder() : STATE_DeactivateSleepSolder();
    }
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
