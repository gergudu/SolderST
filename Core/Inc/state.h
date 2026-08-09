/**
 * @file state.h
 * @brief Хедер управления состояниями и логикой меню
 */

#ifndef STATE_H
#define STATE_H

#include <stdint.h>
#include <stdbool.h>

/* =======================================================================
    ТИПЫ ДАННЫХ
   ======================================================================= */

typedef enum {
    SYS_MODE_MAIN_SOLDER = 0,
    SYS_MODE_MAIN_DESOLDER,
    SYS_MODE_SERVICE,
    SYS_MODE_EXPERT,
    SYS_MODE_EXPERT_WARN,   // экран предупреждения перед Expert
    SYS_MODE_MAX
} SystemMode_t;

/* =======================================================================
    СТРУКТУРА ЭЛЕМЕНТА СЕРВИСНОГО МЕНЮ
   ======================================================================= */

typedef void (*MenuSetterFn)(uint16_t value);
typedef void (*MenuNotifyFn)(void);

typedef struct {
    const char *label;
    uint16_t *valueSolder;
    uint16_t *valueDesolder;
    MenuSetterFn setterSolder;
    MenuSetterFn setterDesolder;
    MenuNotifyFn onChanged;
    uint16_t min;
    uint16_t max;
    uint16_t step;
} ServiceMenuItem_t;

/* =======================================================================
    УПРАВЛЕНИЕ РЕЖИМАМИ И ЭКРАНОМ
   ======================================================================= */

SystemMode_t STATE_GetMode(void);
void         STATE_SetMode(SystemMode_t mode);
bool         STATE_CheckAndResetDirty(void);
void         STATE_ResetMenu(void);

/* =======================================================================
    ЛОГИКА ВЗАИМОДЕЙСТВИЯ (КНОПКИ)
   ======================================================================= */

void STATE_MenuNavigate(int8_t dir);
void STATE_MenuToggleEdit(void);
void STATE_MenuClick(void);
void STATE_ServiceToggleTool(void);

/* Expert меню */
void STATE_EnterExpertWarn(void);
void STATE_EnterExpert(void);
void STATE_ExitExpert(void);
uint8_t     STATE_GetExpertCursor(void);
bool        STATE_IsExpertEditing(void);
void        STATE_ExpertMenuNavigate(int8_t dir);
void        STATE_ExpertMenuNavigateAccel(int16_t delta);
void        STATE_ExpertMenuToggleEdit(void);
void        STATE_ExpertDoReset(void);
uint8_t     STATE_GetExpertTotalItems(void);
const char* STATE_GetExpertItemLabel(uint8_t idx);
uint16_t    STATE_GetExpertItemValue(uint8_t idx);

/* =======================================================================
    ЛОГИКА СНА (ПРОБРОС В CONFIG)
   ======================================================================= */

void STATE_ActivateSleepSolder(void);
void STATE_DeactivateSleepSolder(void);
void STATE_ActivateSleepDesolder(void);
void STATE_DeactivateSleepDesolder(void);
void STATE_SyncSleepTimeouts(void);

/* =======================================================================
    ГЕТТЕРЫ ДЛЯ ОТРИСОВКИ (UI)
   ======================================================================= */

uint8_t     STATE_GetMenuCursor(void);
uint8_t     STATE_GetMenuTop(void);
bool        STATE_IsEditing(void);
bool        STATE_IsServiceEditingSolder(void);
uint8_t     STATE_GetMenuTotalItems(void);
uint8_t     STATE_GetMenuVisibleRows(void);
const char* STATE_GetItemLabel(uint8_t idx);
uint16_t    STATE_GetItemValue(uint8_t idx);
bool        STATE_IsServiceItemAction(uint8_t idx);
const ServiceMenuItem_t* STATE_GetServiceMenuItem(uint8_t idx);
bool STATE_IsExpertWarn(void);

#endif // STATE_H
