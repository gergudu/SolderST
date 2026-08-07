/**
 * @file state.c
 * @brief Управление состояниями и логикой меню
 */

#include "state.h"
#include "config.h"
#include "buttons.h"
#include "heater.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

bool g_UI_NeedsClear = true;

/* =========================================================================
   EXPERT МЕНЮ
   ========================================================================= */

typedef struct {
    const char   *label;
    uint16_t     *valueSolder;
    uint16_t     *valueDesolder;
    MenuSetterFn  setterSolder;
    MenuSetterFn  setterDesolder;
    uint16_t      min;
    uint16_t      max;
    uint16_t      step;
    bool          is_action;   // Сброс / Выход — не редактируемые значения
} ExpertMenuItem_t;

static const ExpertMenuItem_t g_ExpertMenu[] = {
    {"Выход", NULL, NULL, NULL, NULL, 0, 0, 0, true},
    {"Сброс", NULL, NULL, NULL, NULL, 0, 0, 0, true},
    {"Kp",    &g_ServiceSettings.KpSolder,    &g_ServiceSettings.KpDesolder,
              CONFIG_SetKpSolderer, CONFIG_SetKpDesolder,    0, 10000, 1, false},
    {"Ki",    &g_ServiceSettings.KiSolder,    &g_ServiceSettings.KiDesolder,
              CONFIG_SetKiSolderer, CONFIG_SetKiDesolder,    0, 10000, 1, false},
    {"Kd",    &g_ServiceSettings.KdSolder,    &g_ServiceSettings.KdDesolder,
              CONFIG_SetKdSolderer, CONFIG_SetKdDesolder,    0, 10000, 1, false},
    {"Slope", &g_ServiceSettings.slopeSolder, &g_ServiceSettings.slopeDesolder,
              CONFIG_SetSlopeSolder, CONFIG_SetSlopeDesolder, 50, 100, 1, false},
    {"Bias",  &g_ServiceSettings.biasSolder,  &g_ServiceSettings.biasDesolder,
              CONFIG_SetBiasSolder,  CONFIG_SetBiasDesolder,  100, 400, 1, false},
};

#define EXPERT_TOTAL_ITEMS  (sizeof(g_ExpertMenu) / sizeof(g_ExpertMenu[0]))

static uint8_t g_ExpertCursor  = 0;
static bool    g_ExpertEditing = false;

void STATE_EnterExpertWarn(void) {
    STATE_SetMode(SYS_MODE_EXPERT_WARN);
}

void STATE_EnterExpert(void) {
    g_ExpertCursor  = 0;
    g_ExpertEditing = false;
    g_UI_NeedsClear = true;
    STATE_SetMode(SYS_MODE_EXPERT);
}

void STATE_ExitExpert(void) {
    g_ExpertEditing = false;
    STATE_SetMode(SYS_MODE_SERVICE);
    STATE_ResetMenu();
}

uint8_t STATE_GetExpertCursor(void)      { return g_ExpertCursor; }
bool    STATE_IsExpertEditing(void)      { return g_ExpertEditing; }
uint8_t STATE_GetExpertTotalItems(void)  { return (uint8_t)EXPERT_TOTAL_ITEMS; }

const char* STATE_GetExpertItemLabel(uint8_t idx) {
    return (idx < EXPERT_TOTAL_ITEMS) ? g_ExpertMenu[idx].label : "";
}

uint16_t STATE_GetExpertItemValue(uint8_t idx) {
    if (idx >= EXPERT_TOTAL_ITEMS || g_ExpertMenu[idx].is_action) return 0;
    return g_WorkFlags.tool ? *g_ExpertMenu[idx].valueSolder
                            : *g_ExpertMenu[idx].valueDesolder;
}

void STATE_ExpertMenuNavigate(int8_t dir) {
    if (g_ExpertEditing) {
        /* При редактировании dir=-1 (UP) должен УВЕЛИЧИВАТЬ значение,
           dir=+1 (DN) — УМЕНЬШАТЬ. Инвертируем знак. */
        STATE_ExpertMenuNavigateAccel(-dir);
    } else {
        /* dir > 0 = DN (индекс растёт), dir < 0 = UP */
        int16_t next = (int16_t)g_ExpertCursor + dir;
        if (next < 0) next = (int16_t)EXPERT_TOTAL_ITEMS - 1;
        if (next >= (int16_t)EXPERT_TOTAL_ITEMS) next = 0;
        g_ExpertCursor = (uint8_t)next;
        /* Полная очистка экрана здесь не нужна — UI_DrawExpertMenu
           сам перерисовывает только затронутые строки. */
    }
}

void STATE_ExpertMenuNavigateAccel(int16_t delta) {
    /* При навигации (не редактировании) — игнорируем, SHORT уже обработан */
    if (!g_ExpertEditing) return;
    if (g_ExpertCursor >= EXPERT_TOTAL_ITEMS) return;
    const ExpertMenuItem_t *item = &g_ExpertMenu[g_ExpertCursor];
    if (item->is_action) return;

    MenuSetterFn setter = g_WorkFlags.tool ? item->setterSolder : item->setterDesolder;
    if (!setter) return;

    uint16_t cur = g_WorkFlags.tool ? *item->valueSolder : *item->valueDesolder;
    int32_t  nv  = (int32_t)cur + delta;
    if (nv < item->min) nv = item->min;
    if (nv > item->max) nv = item->max;
    if (cur != (uint16_t)nv) setter((uint16_t)nv);
}

void STATE_ExpertMenuToggleEdit(void) {
    if (g_ExpertMenu[g_ExpertCursor].is_action) return;
    g_ExpertEditing = !g_ExpertEditing;
}

void STATE_ExpertDoReset(void) {
    CONFIG_ResetToDefaults(g_WorkFlags.tool);
    g_UI_NeedsClear = true;
}

/* --- Сервисное меню --- */
static ServiceMenuItem_t g_ServiceMenu[] = {
    {"Выход",  NULL, NULL, NULL, NULL, NULL, 0, 0, 0},

    {"PreSleep", &g_ServiceSettings.preSleepTimeoutSolder, &g_ServiceSettings.preSleepTimeoutDesolder,
     CONFIG_SetPreSleepTimeoutSolder, CONFIG_SetPreSleepTimeoutDesolder, STATE_SyncSleepTimeouts, 0, 30, 1},

    {"SleepTemp", &g_ServiceSettings.sleepTempSolder, &g_ServiceSettings.sleepTempDesolder,
     CONFIG_SetSleepTempSolder, CONFIG_SetSleepTempDesolder, NULL, 150, 450, 5},

    {"Standby",  &g_ServiceSettings.sleepTimeoutSolder, &g_ServiceSettings.sleepTimeoutDesolder,
     CONFIG_SetSleepTimeoutSolder, CONFIG_SetSleepTimeoutDesolder, STATE_SyncSleepTimeouts, 0, 60, 1},

    {"Expert", NULL, NULL, NULL, NULL, NULL, 0, 0, 0},
};

#define MENU_TOTAL_ITEMS    (sizeof(g_ServiceMenu) / sizeof(g_ServiceMenu[0]))
#define MENU_VISIBLE_ROWS   2

/* --- Состояние меню --- */
static SystemMode_t g_CurrentMode = SYS_MODE_MAIN_SOLDER;
static uint8_t g_MenuCursor = 0;
static uint8_t g_MenuTop    = 0;
static bool    g_IsEditing  = false;
static bool g_ServiceExpertWarn = false;

/* --- Логика скролла и изменения значения --- */
static void STATE_UpdateScroll(void) {
    if (g_MenuCursor < g_MenuTop) g_MenuTop = g_MenuCursor;
    else if (g_MenuCursor >= (g_MenuTop + MENU_VISIBLE_ROWS)) g_MenuTop = g_MenuCursor - MENU_VISIBLE_ROWS + 1;
}

/**
 * @brief Применение изменения значения с вызовом сеттера ДО прямого изменения
 */
static void STATE_ApplyValue(int8_t dir) {
    const ServiceMenuItem_t *item = &g_ServiceMenu[g_MenuCursor];
    MenuSetterFn setter = g_WorkFlags.tool ? item->setterSolder : item->setterDesolder;

    // Для пункта Expert нет сеттера
    if (!setter) return;

    // Получаем текущее значение
    uint16_t current = g_WorkFlags.tool ? *item->valueSolder : *item->valueDesolder;

    // Вычисляем новое значение
    int32_t newVal = (int32_t)current + (dir * item->step);
    if (newVal < (int32_t)item->min) newVal = item->min;
    if (newVal > (int32_t)item->max) newVal = item->max;

    // Если значение действительно меняется
    if (current != (uint16_t)newVal) {
        // ВЫЗЫВАЕМ СЕТТЕР - он сам обновит значение, установит dirty-флаг и запустит таймер
        setter((uint16_t)newVal);

        // Вызываем колбэк изменения, если есть
        if (item->onChanged) item->onChanged();
    }
}

/* --- Публичный API --- */
SystemMode_t STATE_GetMode(void) { return g_CurrentMode; }

void STATE_SetMode(SystemMode_t mode) {
    if (mode < SYS_MODE_MAX && mode != g_CurrentMode) {
        g_CurrentMode = mode;
        g_UI_NeedsClear = true;
    }
}

void STATE_ResetMenu(void) {
    g_MenuCursor = 0;
    g_MenuTop = 0;
    g_IsEditing = false;
}

void STATE_MenuNavigate(int8_t dir) {
    if (g_IsEditing) {
        STATE_ApplyValue(dir);
    } else {
        uint8_t oldCursor = g_MenuCursor;
        int16_t next = (int16_t)g_MenuCursor + dir;
        /* Зацикливание */
        if (next < 0) next = (int16_t)MENU_TOTAL_ITEMS - 1;
        if (next >= (int16_t)MENU_TOTAL_ITEMS) next = 0;
        g_MenuCursor = (uint8_t)next;

        if (oldCursor != g_MenuCursor) {
            /* Полная очистка экрана здесь не нужна — UI_DrawServiceMenu
               сам перерисовывает только затронутые (и, при прокрутке,
               видимые) строки. */
            g_ServiceExpertWarn = (STATE_GetItemLabel(g_MenuCursor) &&
                                   strcmp(STATE_GetItemLabel(g_MenuCursor), "Expert") == 0);
        }
        STATE_UpdateScroll();
    }
}

void STATE_MenuToggleEdit(void) {
    g_IsEditing = !g_IsEditing;
    // Убрано принудительное сохранение при выходе - теперь всё через таймер
}

void STATE_MenuClick(void) { STATE_MenuToggleEdit(); }

void STATE_ServiceToggleTool(void) {
    bool want_solder = !g_WorkFlags.tool;
    if (!HEATER_IsToolOk(want_solder)) return; /* инструмент неисправен/не подключен */
    g_WorkFlags.tool = want_solder;
    g_UI_NeedsClear = true;
}

/* --- Проброс функций сна --- */
void STATE_ActivateSleepSolder(void)   { CONFIG_ActivateSleepCounterSolder(); }
void STATE_DeactivateSleepSolder(void) { CONFIG_DeactivateSleepCounterSolder(); }
void STATE_ActivateSleepDesolder(void)    { CONFIG_ActivateSleepCounterDesolder(); }
void STATE_DeactivateSleepDesolder(void)  { CONFIG_DeactivateSleepCounterDesolder(); }
void STATE_SyncSleepTimeouts(void) {
    if (CONFIG_IsSleepCounterActiveSolder()) CONFIG_ActivateSleepCounterSolder();
    if (CONFIG_IsSleepCounterActiveDesolder())  CONFIG_ActivateSleepCounterDesolder();
}

/* --- Геттеры --- */
uint8_t     STATE_GetMenuCursor(void)        { return g_MenuCursor; }
uint8_t     STATE_GetMenuTop(void)           { return g_MenuTop; }
bool        STATE_IsEditing(void)            { return g_IsEditing; }
bool        STATE_IsServiceEditingSolder(void) { return g_WorkFlags.tool; }
uint8_t     STATE_GetMenuTotalItems(void)    { return MENU_TOTAL_ITEMS; }
uint8_t     STATE_GetMenuVisibleRows(void)   { return MENU_VISIBLE_ROWS; }
const char* STATE_GetItemLabel(uint8_t idx)  { return (idx < MENU_TOTAL_ITEMS) ? g_ServiceMenu[idx].label : ""; }
uint16_t STATE_GetItemValue(uint8_t idx) {
    if (idx >= MENU_TOTAL_ITEMS) return 0;
    return g_WorkFlags.tool ? *g_ServiceMenu[idx].valueSolder
                            : *g_ServiceMenu[idx].valueDesolder;
}
const ServiceMenuItem_t* STATE_GetServiceMenuItem(uint8_t idx) {
    if (idx >= MENU_TOTAL_ITEMS) return NULL;
    return &g_ServiceMenu[idx];
}
bool STATE_IsExpertWarn(void) { return g_ServiceExpertWarn; }

bool STATE_CheckAndResetDirty(void) {
    bool tmp = g_UI_NeedsClear;
    g_UI_NeedsClear = false;
    return tmp;
}
