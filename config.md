config.c — реализацию работы с EEPROM и управлением настройками паяльной станции.

Вот что здесь реализовано и как это работает:

1. Дефолты

CONFIG_TempDefaults и CONFIG_ServiceDefaults — значения по умолчанию для температуры, PID, таймаутов и флагов.

CONFIG_EEPROMDefaults — полностью подготовленная структура для первичной записи в EEPROM.

CONFIG_WorkFlagsDefaults и CONFIG_SleepCountersDefaults — значения по умолчанию для RAM-флагов и счетчиков сна.

2. Глобальные переменные

RAM структуры для работы:

WorkFlags_t g_WorkFlags;
TempSettings_t g_TempSettings;
ServiceSettings_t g_ServiceSettings;
SleepCounters_t g_SleepCounters;
volatile DirtyFlags_t g_DirtyFlags;
volatile uint16_t g_SaveDelayCounter;
uint16_t g_tCurrentSold;
uint16_t g_tCurrentVac;

Буфер EEPROM:

static EEPROM_Config_t g_EEPROMConfig;
3. Внутренние функции

CRC16 — функция CONFIG_CRC16_CCITT, проверка целостности данных.

CONFIG_CalculateCurrentCRC — пересчет CRC для текущего буфера EEPROM.

CONFIG_UpdateField — обновление конкретного поля в EEPROM и пересчет CRC.

CONFIG_LoadFromBuffer — перенос данных из g_EEPROMConfig в рабочие структуры RAM.

4. Валидация

CONFIG_ValidateTempSettings — проверка и ограничение значений температуры.

CONFIG_ValidateServiceSettings — проверка всех сервисных настроек (PID, таймауты, slope/bias).

5. Инициализация

CONFIG_Init:

Загружает дефолты в RAM.

Пытается прочитать EEPROM (3 попытки).

Если EEPROM невалидна, записывает дефолты в EEPROM и пересчитывает CRC.

Валидирует все настройки и сбрасывает g_DirtyFlags.

6. EEPROM LOAD / SAVE

LOAD: CONFIG_LoadFromEEPROM — чтение всей структуры, проверка magic, версии и CRC.

SAVE: CONFIG_SaveToEEPROM — инкрементальная запись только изменённых полей (DirtyFlags).
Используются макросы:

UPDATE_SERVICE_FIELD(var)
UPDATE_TEMP_FIELD(var)
7. Работа с настройками

Температуры и пресеты: CONFIG_SetTargetSold, CONFIG_SetPresetVac и геттеры.

Сервисные параметры: PID (CONFIG_SetKpSold и др.), slope/bias, таймауты сна.

Флаги: сон/зуммер (CONFIG_SetSleepEnabledSold, CONFIG_SetBuzzerEnabled).

8. Счётчики сна

Управление активностью и декремент:

CONFIG_DecrementSleepCounters();
CONFIG_ActivateSleepCounterSold();
CONFIG_ResetSleepCounterVac();
9. Проверка изменений

CONFIG_IsDirty() — проверка наличия изменённых полей для записи.

CONFIG_SaveServiceSettings() — записывает все сервисные настройки в EEPROM.

🔹 Итог

config.c полностью управляет: RAM-настройками, сохранением/загрузкой EEPROM, инкрементальной записью и проверкой CRC.

Поддерживает все фичи из config.h.

Полностью готов к использованию в проекте.