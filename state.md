state.c — реализация логики меню и управления режимами. Давай разберём, что здесь происходит и как всё работает вместе с config.c и UI.

1️⃣ Глобальные состояния
bool g_UI_NeedsClear = true;
static SystemMode_t g_CurrentMode = SYS_MODE_MAIN_SOLD;
static uint8_t g_MenuCursor = 0;
static uint8_t g_MenuTop = 0;
static bool    g_IsEditing = false;

g_UI_NeedsClear — флаг, чтобы экран полностью перерисовать.

g_CurrentMode — текущий режим (SOLD, VAC, SERVICE).

g_MenuCursor — текущий выбранный пункт меню.

g_MenuTop — верхний видимый пункт меню (scroll).

g_IsEditing — true, если редактируем значение, иначе — навигация.

2️⃣ Сервисное меню (g_ServiceMenu)
typedef struct {
    const char *label;
    uint16_t *valueSold;
    uint16_t *valueVac;
    MenuSetterFn setterSold;
    MenuSetterFn setterVac;
    MenuNotifyFn onChanged;
    uint16_t min;
    uint16_t max;
    uint16_t step;
} ServiceMenuItem_t;

Для каждого пункта меню хранятся:

Метка (label)

Указатели на значения Sold/Vac

Функции установки (setterSold / setterVac)

Функция уведомления (onChanged)

Минимум, максимум, шаг

Пример пункта:

{"PreSleep", &g_ServiceSettings.preSleepTimeoutSold, &g_ServiceSettings.preSleepTimeoutVac,
 CONFIG_SetPreSleepTimeoutSold, CONFIG_SetPreSleepTimeoutVac, STATE_SyncSleepTimeouts, 0, 30, 1}

Указывает на соответствующие переменные из config.c и связывает с функциями изменения.

3️⃣ Логика скролла
static void STATE_UpdateScroll(void) {
    if (g_MenuCursor < g_MenuTop) g_MenuTop = g_MenuCursor;
    else if (g_MenuCursor >= (g_MenuTop + MENU_VISIBLE_ROWS))
        g_MenuTop = g_MenuCursor - MENU_VISIBLE_ROWS + 1;
}

Обеспечивает корректный видимый диапазон пунктов меню при движении курсора.

4️⃣ Изменение значений (редактирование)
static void STATE_ApplyValue(int8_t dir) {
    const ServiceMenuItem_t *item = &g_ServiceMenu[g_MenuCursor];
    uint16_t *valPtr = g_WorkFlags.tool ? item->valueSold : item->valueVac;
    MenuSetterFn setter = g_WorkFlags.tool ? item->setterSold : item->setterVac;
    int32_t newVal = (int32_t)(*valPtr) + (dir * item->step);

    if (newVal < (int32_t)item->min) newVal = item->min;
    if (newVal > (int32_t)item->max) newVal = item->max;

    if (*valPtr != (uint16_t)newVal) {
        *valPtr = (uint16_t)newVal;
        if (setter) setter((uint16_t)newVal);
        if (item->onChanged) item->onChanged();
    }
}

Если активен режим редактирования (g_IsEditing = true), кнопки вверх/вниз изменяют значение.

Используется шаг step и проверка на границы [min,max].

При изменении вызывается setter и onChanged (например, синхронизация таймеров сна).

5️⃣ Навигация и редактирование меню
void STATE_MenuNavigate(int8_t dir) {
    if (g_IsEditing) STATE_ApplyValue(dir);
    else {
        int16_t next = (int16_t)g_MenuCursor + dir;
        if (next >= 0 && next < STATE_GetMenuTotalItems()) g_MenuCursor = (uint8_t)next;
        g_UI_NeedsClear = true;
        STATE_UpdateScroll();
    }
}

void STATE_MenuToggleEdit(void) {
    g_IsEditing = !g_IsEditing;
    if (!g_IsEditing && CONFIG_IsDirty()) CONFIG_SaveServiceSettings();
}

void STATE_MenuClick(void) { STATE_MenuToggleEdit(); }

MenuNavigate — в режиме редактирования меняет значение, иначе — перемещает курсор.

MenuToggleEdit / MenuClick — вход/выход из режима редактирования; при выходе сохраняются изменения в EEPROM.

6️⃣ Смена ручки в сервисном меню
void STATE_ServiceToggleTool(void) {
    g_WorkFlags.tool = !g_WorkFlags.tool;
    g_UI_NeedsClear = true;
}

Переключает контекст между SOLD и VAC.

UI перерисовывается.

7️⃣ Счётчики сна
void STATE_ActivateSleepSold(void)   { CONFIG_ActivateSleepCounterSold(); }
void STATE_DeactivateSleepSold(void) { CONFIG_DeactivateSleepCounterSold(); }
void STATE_ActivateSleepVac(void)    { CONFIG_ActivateSleepCounterVac(); }
void STATE_DeactivateSleepVac(void)  { CONFIG_DeactivateSleepCounterVac(); }

void STATE_SyncSleepTimeouts(void) {
    if (CONFIG_IsSleepCounterActiveSold()) CONFIG_ActivateSleepCounterSold();
    if (CONFIG_IsSleepCounterActiveVac())  CONFIG_ActivateSleepCounterVac();
}

Проброс в config.c для управления таймерами сна.

SyncSleepTimeouts нужен, чтобы при редактировании таймаутов активные счётчики корректно синхронизировались.

8️⃣ Геттеры для UI

Курсор, scroll, режим редактирования:

uint8_t  STATE_GetMenuCursor(void);
uint8_t  STATE_GetMenuTop(void);
bool     STATE_IsEditing(void);
bool     STATE_IsServiceEditingSold(void);

Получение текста и значения пунктов:

const char* STATE_GetItemLabel(uint8_t idx);
uint16_t    STATE_GetItemValue(uint8_t idx);

Общее количество пунктов и видимые строки:

uint8_t STATE_GetMenuTotalItems(void);
uint8_t STATE_GetMenuVisibleRows(void);

Флаг перерисовки:

bool STATE_CheckAndResetDirty(void);