#include "config.h"
#include "eeprom_i2c.h"
#include <string.h>


/* Глобальные переменные */
volatile WorkFlags_t g_WorkFlags;
TempSettings_t g_TempSettings;
ServiceSettings_t g_ServiceSettings;
SleepCounters_t g_SleepCounters;
volatile DirtyFlags_t g_DirtyFlags;
volatile uint16_t g_SaveDelayCounter = 0;
volatile uint32_t g_EepromFlashUntil = 0;
uint16_t g_tCurrentSolder = 0;
uint16_t g_tCurrentDesolder = 0;

static EEPROM_Config_t g_EEPROMConfig;

/* Дефолты */
static const EEPROM_Config_t CONFIG_EEPROMDefaults = {
    .magic = 0xA5C7,
    .version = 0x04,
    .tempSettings = {300, 350, 400, 300, 300, 350, 400, 300},
    .serviceSettings = {
        .flags = FLAG_SLEEP_SOLDER_EN | FLAG_SLEEP_DESOLDER_EN | FLAG_BZ_EN,
        .sleepTempSolder = 150, .sleepTempDesolder = 150,
        .slopeSolder = 72, .biasSolder = 217, .slopeDesolder = 72, .biasDesolder = 217,
        .preSleepTimeoutSolder = 5, .sleepTimeoutSolder = 10,
        .preSleepTimeoutDesolder = 5, .sleepTimeoutDesolder = 10,
        .KpSolder = 100, .KiSolder = 50, .KdSolder = 20,
		.KpDesolder = 100, .KiDesolder = 50, .KdDesolder = 20
    }
};

/* Метаданные для записи */
typedef struct {
    uint16_t offset;
    void* pRAM;
    uint8_t size;
} ConfigMap_t;

#define CFG_ITEM(g_st, ee_st, fld) { offsetof(EEPROM_Config_t, ee_st.fld), &g_st.fld, sizeof(g_st.fld) }

static const ConfigMap_t CONFIG_Map[] = {
    CFG_ITEM(g_TempSettings, tempSettings, preSet1Solder),    CFG_ITEM(g_TempSettings, tempSettings, preSet2Solder),
    CFG_ITEM(g_TempSettings, tempSettings, preSet3Solder),    CFG_ITEM(g_TempSettings, tempSettings, targetSetSolder),
    CFG_ITEM(g_TempSettings, tempSettings, preSet1Desolder),     CFG_ITEM(g_TempSettings, tempSettings, preSet2Desolder),
    CFG_ITEM(g_TempSettings, tempSettings, preSet3Desolder),     CFG_ITEM(g_TempSettings, tempSettings, targetSetDesolder),
    CFG_ITEM(g_ServiceSettings, serviceSettings, flags),       CFG_ITEM(g_ServiceSettings, serviceSettings, sleepTempSolder),
    CFG_ITEM(g_ServiceSettings, serviceSettings, sleepTempDesolder),CFG_ITEM(g_ServiceSettings, serviceSettings, slopeSolder),
    CFG_ITEM(g_ServiceSettings, serviceSettings, biasSolder),    CFG_ITEM(g_ServiceSettings, serviceSettings, slopeDesolder),
    CFG_ITEM(g_ServiceSettings, serviceSettings, biasDesolder),     CFG_ITEM(g_ServiceSettings, serviceSettings, preSleepTimeoutSolder),
    CFG_ITEM(g_ServiceSettings, serviceSettings, sleepTimeoutSolder),
    CFG_ITEM(g_ServiceSettings, serviceSettings, preSleepTimeoutDesolder),
    CFG_ITEM(g_ServiceSettings, serviceSettings, sleepTimeoutDesolder),
    CFG_ITEM(g_ServiceSettings, serviceSettings, KpSolder),      CFG_ITEM(g_ServiceSettings, serviceSettings, KiSolder),      CFG_ITEM(g_ServiceSettings, serviceSettings, KdSolder),
    CFG_ITEM(g_ServiceSettings, serviceSettings, KpDesolder),       CFG_ITEM(g_ServiceSettings, serviceSettings, KiDesolder),       CFG_ITEM(g_ServiceSettings, serviceSettings, KdDesolder)
};

/* Реализация API */
uint16_t CONFIG_CalculateCRC16(const uint8_t *data, uint32_t len) {
    uint16_t crc = CRC16_INIT_VALUE;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (crc << 1) ^ CRC16_CCITT_POLY;
            else crc <<= 1;
        }
    }
    return crc;
}

static uint16_t CONFIG_GetBufferCRC(void) {
    return CONFIG_CalculateCRC16((uint8_t*)&g_EEPROMConfig, offsetof(EEPROM_Config_t, crc16));
}

bool CONFIG_VerifyCRC(void) {
    return g_EEPROMConfig.crc16 == CONFIG_GetBufferCRC();
}

static bool CONFIG_UpdateField(uint16_t offset, const void *data, uint16_t size) {
    if (!EEPROM_I2C_Write(offset, (uint8_t*)data, size)) return false;
    memcpy((uint8_t*)&g_EEPROMConfig + offset, data, size);
    g_EEPROMConfig.crc16 = CONFIG_GetBufferCRC();
    return EEPROM_I2C_Write(offsetof(EEPROM_Config_t, crc16), (uint8_t*)&g_EEPROMConfig.crc16, 2);
}

/* Пишет g_EEPROMConfig целиком в EEPROM (с пересчётом CRC).
   Общая точка записи и для первичной инициализации (пустой/битый EEPROM),
   и для сброса настроек в Expert-меню — один и тот же путь кода. */
static bool CONFIG_WriteWholeStructToEEPROM(void) {
    g_EEPROMConfig.crc16 = CONFIG_GetBufferCRC();
    if (!EEPROM_I2C_Write(0, (uint8_t*)&g_EEPROMConfig, sizeof(EEPROM_Config_t))) {
        return false; /* g_EepromFault уже выставлен внутри EEPROM_I2C_Write */
    }

    /* Сверка чтением: на некоторых платах I2C-транзакция может формально
       завершиться без ошибки (ACK) даже при физически отсутствующей
       микросхеме (плавающая шина, наводки) — по одному коду возврата HAL
       такое не поймать. Перечитываем и сравниваем байт в байт. */
    EEPROM_Config_t verify;
    if (!EEPROM_I2C_Read(0, (uint8_t*)&verify, sizeof(EEPROM_Config_t))
        || memcmp(&verify, &g_EEPROMConfig, sizeof(EEPROM_Config_t)) != 0) {
        g_EepromFault = true;
        return false;
    }
    return true;
}

void CONFIG_Init(void) {
    __disable_irq();
    g_WorkFlags.tool = true;
    __enable_irq();

    /* Прямая проверка присутствия микросхемы на шине — до какой-либо
       попытки прочитать/интерпретировать данные. Самый надёжный способ
       поймать физическое отсутствие EEPROM сразу при старте. */
    EEPROM_I2C_IsPresent();

    if (!CONFIG_LoadFromEEPROM()) {
        g_EEPROMConfig = CONFIG_EEPROMDefaults;
        CONFIG_WriteWholeStructToEEPROM();
        memcpy(&g_TempSettings, &g_EEPROMConfig.tempSettings, sizeof(TempSettings_t));
        memcpy(&g_ServiceSettings, &g_EEPROMConfig.serviceSettings, sizeof(ServiceSettings_t));
    }

    /* Статус вкл/выкл каждого инструмента — персистентный (FLAG_TOOL_EN_*),
       по умолчанию (пустой/битый EEPROM) оба выключены. */
    g_WorkFlags.pwrIsOnSolder = CONFIG_GetToolEnabledSolder();
    g_WorkFlags.pwrIsOnVac    = CONFIG_GetToolEnabledDesolder();

    CONFIG_ValidateAll();
    g_DirtyFlags.all = 0;
}

bool CONFIG_SaveToEEPROM(void) {
    uint32_t dirty = g_DirtyFlags.all;
    if (!dirty) return false;

    for (uint8_t i = 0; i < (sizeof(CONFIG_Map)/sizeof(ConfigMap_t)); i++) {
        if (dirty & (1UL << i)) {
            if (CONFIG_UpdateField(CONFIG_Map[i].offset, CONFIG_Map[i].pRAM, CONFIG_Map[i].size)) {
                __disable_irq();
                g_DirtyFlags.all &= ~(1UL << i);
                __enable_irq();
                return true;
            }
        }
    }
    return false;
}

bool CONFIG_LoadFromEEPROM(void) {
    if (EEPROM_I2C_Read(0, (uint8_t*)&g_EEPROMConfig, sizeof(EEPROM_Config_t))) {
        if (g_EEPROMConfig.magic == 0xA5C7 && CONFIG_VerifyCRC()) {
            memcpy(&g_TempSettings, &g_EEPROMConfig.tempSettings, sizeof(TempSettings_t));
            memcpy(&g_ServiceSettings, &g_EEPROMConfig.serviceSettings, sizeof(ServiceSettings_t));
            return true;
        }
    }
    return false;
}


#define GEN_SETTER(name, field, mn, mx) \
void CONFIG_Set ## name(uint16_t v) { \
    v = CONFIG_Clamp(v, mn, mx); \
    if (g_ServiceSettings.field != v) { \
        g_ServiceSettings.field = v; \
        g_DirtyFlags.field = 1; \
        g_SaveDelayCounter = SAVE_DELAY_TICKS; \
    } \
}

GEN_SETTER(SleepTempSolder, sleepTempSolder, SLEEP_TEMP_MIN, SLEEP_TEMP_MAX)
GEN_SETTER(SleepTempDesolder, sleepTempDesolder, SLEEP_TEMP_MIN, SLEEP_TEMP_MAX)
GEN_SETTER(SlopeSolder, slopeSolder, SLOPE_MIN_SOLDER, SLOPE_MAX_SOLDER)
GEN_SETTER(BiasSolder, biasSolder, BIAS_MIN_SOLDER, BIAS_MAX_SOLDER)
GEN_SETTER(SlopeDesolder, slopeDesolder, SLOPE_MIN_DESOLDER, SLOPE_MAX_DESOLDER)
GEN_SETTER(BiasDesolder, biasDesolder, BIAS_MIN_DESOLDER, BIAS_MAX_DESOLDER)
GEN_SETTER(PreSleepTimeoutSolder, preSleepTimeoutSolder, TIMEOUT_MIN, TIMEOUT_MAX)
GEN_SETTER(SleepTimeoutSolder, sleepTimeoutSolder, TIMEOUT_MIN, TIMEOUT_MAX)
GEN_SETTER(PreSleepTimeoutDesolder, preSleepTimeoutDesolder, TIMEOUT_MIN, TIMEOUT_MAX)
GEN_SETTER(SleepTimeoutDesolder, sleepTimeoutDesolder, TIMEOUT_MIN, TIMEOUT_MAX)
GEN_SETTER(KpSolderer, KpSolder, PID_KP_MIN, PID_KP_MAX)
GEN_SETTER(KiSolderer, KiSolder, PID_KI_MIN, PID_KI_MAX)
GEN_SETTER(KdSolderer, KdSolder, PID_KD_MIN, PID_KD_MAX)
GEN_SETTER(KpDesolder, KpDesolder, PID_KP_MIN, PID_KP_MAX)
GEN_SETTER(KiDesolder, KiDesolder, PID_KI_MIN, PID_KI_MAX)
GEN_SETTER(KdDesolder, KdDesolder, PID_KD_MIN, PID_KD_MAX)

void CONFIG_SetTargetSolder(uint16_t val) {
    val = CONFIG_Clamp(val, SET_MIN, SET_MAX);
    if (g_TempSettings.targetSetSolder != val) {
        g_TempSettings.targetSetSolder = val; g_DirtyFlags.targetSetSolder = 1;
        g_SaveDelayCounter = SAVE_DELAY_TICKS;
    }
}
void CONFIG_SetTargetDesolder(uint16_t val) {
    val = CONFIG_Clamp(val, SET_MIN, SET_MAX);
    if (g_TempSettings.targetSetDesolder != val) {
        g_TempSettings.targetSetDesolder = val; g_DirtyFlags.targetSetDesolder = 1;
        g_SaveDelayCounter = SAVE_DELAY_TICKS;
    }
}
void CONFIG_SetPresetSolder(uint8_t num, uint16_t val) {
    val = CONFIG_Clamp(val, SET_MIN, SET_MAX);
    if (num == 1) { g_TempSettings.preSet1Solder = val; g_DirtyFlags.preSet1Solder = 1; }
    else if (num == 2) { g_TempSettings.preSet2Solder = val; g_DirtyFlags.preSet2Solder = 1; }
    else if (num == 3) { g_TempSettings.preSet3Solder = val; g_DirtyFlags.preSet3Solder = 1; }
    g_SaveDelayCounter = SAVE_DELAY_TICKS;
}
void CONFIG_SetPresetDesolder(uint8_t num, uint16_t val) {
    val = CONFIG_Clamp(val, SET_MIN, SET_MAX);
    if (num == 1) { g_TempSettings.preSet1Desolder = val; g_DirtyFlags.preSet1Desolder = 1; }
    else if (num == 2) { g_TempSettings.preSet2Desolder = val; g_DirtyFlags.preSet2Desolder = 1; }
    else if (num == 3) { g_TempSettings.preSet3Desolder = val; g_DirtyFlags.preSet3Desolder = 1; }
    g_SaveDelayCounter = SAVE_DELAY_TICKS;
}

void CONFIG_SetSleepEnabledSolder(bool en) {
    if (en) g_ServiceSettings.flags |= FLAG_SLEEP_SOLDER_EN; else g_ServiceSettings.flags &= ~FLAG_SLEEP_SOLDER_EN;
    g_DirtyFlags.flags = 1;
    g_SaveDelayCounter = SAVE_DELAY_TICKS;  // Добавили таймер
}
void CONFIG_SetSleepEnabledDesolder(bool en) {
    if (en) g_ServiceSettings.flags |= FLAG_SLEEP_DESOLDER_EN; else g_ServiceSettings.flags &= ~FLAG_SLEEP_DESOLDER_EN;
    g_DirtyFlags.flags = 1;
    g_SaveDelayCounter = SAVE_DELAY_TICKS;  // Добавили таймер
}
void CONFIG_SetBuzzerEnabled(bool en) {
    if (en) g_ServiceSettings.flags |= FLAG_BZ_EN; else g_ServiceSettings.flags &= ~FLAG_BZ_EN;
    g_DirtyFlags.flags = 1;
    g_SaveDelayCounter = SAVE_DELAY_TICKS;  // Добавили таймер
}

void CONFIG_SetToolEnabledSolder(bool en) {
    if (en) g_ServiceSettings.flags |= FLAG_TOOL_EN_SOLDER; else g_ServiceSettings.flags &= ~FLAG_TOOL_EN_SOLDER;
    g_DirtyFlags.flags = 1;
    g_SaveDelayCounter = SAVE_DELAY_TICKS;
}
void CONFIG_SetToolEnabledDesolder(bool en) {
    if (en) g_ServiceSettings.flags |= FLAG_TOOL_EN_DESOLDER; else g_ServiceSettings.flags &= ~FLAG_TOOL_EN_DESOLDER;
    g_DirtyFlags.flags = 1;
    g_SaveDelayCounter = SAVE_DELAY_TICKS;
}
bool CONFIG_GetToolEnabledSolder(void)   { return (g_ServiceSettings.flags & FLAG_TOOL_EN_SOLDER)   != 0; }
bool CONFIG_GetToolEnabledDesolder(void) { return (g_ServiceSettings.flags & FLAG_TOOL_EN_DESOLDER) != 0; }

void CONFIG_ResetToDefaults(bool solder_tool) {
    if (solder_tool) {
        g_ServiceSettings.slopeSolder          = CONFIG_EEPROMDefaults.serviceSettings.slopeSolder;
        g_ServiceSettings.biasSolder           = CONFIG_EEPROMDefaults.serviceSettings.biasSolder;
        g_ServiceSettings.KpSolder             = CONFIG_EEPROMDefaults.serviceSettings.KpSolder;
        g_ServiceSettings.KiSolder             = CONFIG_EEPROMDefaults.serviceSettings.KiSolder;
        g_ServiceSettings.KdSolder             = CONFIG_EEPROMDefaults.serviceSettings.KdSolder;
        g_ServiceSettings.preSleepTimeoutSolder = CONFIG_EEPROMDefaults.serviceSettings.preSleepTimeoutSolder;
        g_ServiceSettings.sleepTempSolder       = CONFIG_EEPROMDefaults.serviceSettings.sleepTempSolder;
        g_ServiceSettings.sleepTimeoutSolder    = CONFIG_EEPROMDefaults.serviceSettings.sleepTimeoutSolder;
        g_TempSettings.preSet1Solder           = CONFIG_EEPROMDefaults.tempSettings.preSet1Solder;
        g_TempSettings.preSet2Solder           = CONFIG_EEPROMDefaults.tempSettings.preSet2Solder;
        g_TempSettings.preSet3Solder           = CONFIG_EEPROMDefaults.tempSettings.preSet3Solder;
    } else {
        g_ServiceSettings.slopeDesolder          = CONFIG_EEPROMDefaults.serviceSettings.slopeDesolder;
        g_ServiceSettings.biasDesolder           = CONFIG_EEPROMDefaults.serviceSettings.biasDesolder;
        g_ServiceSettings.KpDesolder             = CONFIG_EEPROMDefaults.serviceSettings.KpDesolder;
        g_ServiceSettings.KiDesolder             = CONFIG_EEPROMDefaults.serviceSettings.KiDesolder;
        g_ServiceSettings.KdDesolder             = CONFIG_EEPROMDefaults.serviceSettings.KdDesolder;
        g_ServiceSettings.preSleepTimeoutDesolder = CONFIG_EEPROMDefaults.serviceSettings.preSleepTimeoutDesolder;
        g_ServiceSettings.sleepTempDesolder       = CONFIG_EEPROMDefaults.serviceSettings.sleepTempDesolder;
        g_ServiceSettings.sleepTimeoutDesolder    = CONFIG_EEPROMDefaults.serviceSettings.sleepTimeoutDesolder;
        g_TempSettings.preSet1Desolder           = CONFIG_EEPROMDefaults.tempSettings.preSet1Desolder;
        g_TempSettings.preSet2Desolder           = CONFIG_EEPROMDefaults.tempSettings.preSet2Desolder;
        g_TempSettings.preSet3Desolder           = CONFIG_EEPROMDefaults.tempSettings.preSet3Desolder;
    }

    /* Тот же путь записи, что и при первичной инициализации EEPROM
       в CONFIG_Init — без очереди dirty-флагов и без таймера
       отложенного сохранения (g_SaveDelayCounter). */
    g_EEPROMConfig.tempSettings    = g_TempSettings;
    g_EEPROMConfig.serviceSettings = g_ServiceSettings;
    CONFIG_WriteWholeStructToEEPROM();

    /* Поля только что записаны — незачем оставлять их dirty
       и ждать таймер отложенного сохранения. */
    __disable_irq();
    g_DirtyFlags.all = 0;
    __enable_irq();
}

void CONFIG_ValidateAll(void) {
    g_TempSettings.preSet1Solder = CONFIG_Clamp(g_TempSettings.preSet1Solder, SET_MIN, SET_MAX);
    g_TempSettings.targetSetSolder = CONFIG_Clamp(g_TempSettings.targetSetSolder, SET_MIN, SET_MAX);
    g_ServiceSettings.sleepTempSolder = CONFIG_Clamp(g_ServiceSettings.sleepTempSolder, SLEEP_TEMP_MIN, SLEEP_TEMP_MAX);
}

uint16_t CONFIG_GetPresetSolder(uint8_t num) {
    if (num == 2) return g_TempSettings.preSet2Solder;
    if (num == 3) return g_TempSettings.preSet3Solder;
    return g_TempSettings.preSet1Solder;
}
uint16_t CONFIG_GetPresetDesolder(uint8_t num) {
    if (num == 2) return g_TempSettings.preSet2Desolder;
    if (num == 3) return g_TempSettings.preSet3Desolder;
    return g_TempSettings.preSet1Desolder;
}
/* CONFIG_GetEffectiveTargetSolder/Desolder удалены — см. пояснение
   в config.h. Правильная версия — HEATER_GetEffectiveTargetSolder/Desolder
   в heater.c. */

void CONFIG_DecrementSleepCounters(void) {
    if (g_SleepCounters.activeSolder && g_SleepCounters.counterSolder > 0) {
        g_SleepCounters.counterSolder--;
        if (g_SleepCounters.counterSolder == 0) {
            HEATER_OnSleepTickSolder();
        }
    }
    if (g_SleepCounters.activeDesolder && g_SleepCounters.counterDesolder > 0) {
        g_SleepCounters.counterDesolder--;
        if (g_SleepCounters.counterDesolder == 0) {
            HEATER_OnSleepTickDesolder();
        }
    }
}

void CONFIG_ResetSleepCounterToSleep(bool is_solder) {
    if (is_solder) {
        g_SleepCounters.counterSolder = CONFIG_MinutesToTicks(g_ServiceSettings.sleepTimeoutSolder);
        g_SleepCounters.activeSolder  = true;
    } else {
        g_SleepCounters.counterDesolder = CONFIG_MinutesToTicks(g_ServiceSettings.sleepTimeoutDesolder);
        g_SleepCounters.activeDesolder  = true;
    }
}
void CONFIG_ResetSleepCounterSolder(void) { g_SleepCounters.counterSolder = CONFIG_MinutesToTicks(g_ServiceSettings.preSleepTimeoutSolder); }
void CONFIG_ResetSleepCounterDesolder(void)  { g_SleepCounters.counterDesolder = CONFIG_MinutesToTicks(g_ServiceSettings.preSleepTimeoutDesolder); }
void CONFIG_ActivateSleepCounterSolder(void) { g_SleepCounters.activeSolder = true; }
void CONFIG_DeactivateSleepCounterSolder(void) { g_SleepCounters.activeSolder = false; g_SleepCounters.counterSolder = 0; }
void CONFIG_ActivateSleepCounterDesolder(void) { g_SleepCounters.activeDesolder = true; }
void CONFIG_DeactivateSleepCounterDesolder(void) { g_SleepCounters.activeDesolder = false; g_SleepCounters.counterDesolder = 0; }
bool CONFIG_IsSleepCounterActiveSolder(void) { return g_SleepCounters.activeSolder; }
bool CONFIG_IsSleepCounterActiveDesolder(void)  { return g_SleepCounters.activeDesolder; }
bool CONFIG_IsDirty(void) { return g_DirtyFlags.all != 0; }
void CONFIG_SaveServiceSettings(void) { CONFIG_SaveToEEPROM(); }
