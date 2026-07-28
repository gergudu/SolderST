/**
 * @file config.h
 * @brief Управление конфигурацией паяльной станции
 */
#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* =======================================================================
 КОНСТАНТЫ
 ======================================================================= */

#define SAVE_DELAY_TICKS   150
#define SET_MIN            50
#define SET_MAX            450

#define SLEEP_TEMP_MIN     50
#define SLEEP_TEMP_MAX     200

#define TIMEOUT_MIN        1
#define TIMEOUT_MAX        30

#define SLOPE_MIN_SOLDER     50
#define SLOPE_MAX_SOLDER     100
#define SLOPE_MIN_DESOLDER   50
#define SLOPE_MAX_DESOLDER   100

#define BIAS_MIN_SOLDER      100
#define BIAS_MAX_SOLDER      400
#define BIAS_MIN_DESOLDER    100
#define BIAS_MAX_DESOLDER    400

#define PID_KP_MIN         0
#define PID_KP_MAX         10000
#define PID_KI_MIN         0
#define PID_KI_MAX         10000
#define PID_KD_MIN         0
#define PID_KD_MAX         10000

#define FLAG_SLEEP_SOLDER_EN  (1 << 0)
#define FLAG_SLEEP_DESOLDER_EN   (1 << 1)
#define FLAG_BZ_EN          (1 << 2)

#define CRC16_CCITT_POLY    0x1021
#define CRC16_INIT_VALUE    0xFFFF

/* =======================================================================
 СТРУКТУРЫ
 ======================================================================= */

/* RAM-флаги рабочего состояния */
typedef struct {
	bool tool;
	bool pwrIsOnSolder;
	bool pwrIsOnVac;
	bool serviceMode;
	bool preSleepSolder;
	bool preSleepDesolder;
	volatile bool IsMove;
} WorkFlags_t;

/* EEPROM-пакованные настройки температуры */
typedef struct {
	uint16_t preSet1Solder, preSet2Solder, preSet3Solder;
	uint16_t targetSetSolder;
	uint16_t preSet1Desolder, preSet2Desolder, preSet3Desolder;
	uint16_t targetSetDesolder;
} TempSettings_t;

/* EEPROM-пакованные сервисные настройки */
typedef struct {
	uint8_t flags;
	uint16_t sleepTempSolder, sleepTempDesolder;
	uint16_t slopeSolder, biasSolder;
	uint16_t slopeDesolder, biasDesolder;
	uint16_t preSleepTimeoutSolder, sleepTimeoutSolder;
	uint16_t preSleepTimeoutDesolder, sleepTimeoutDesolder;
	uint16_t KpSolder, KiSolder, KdSolder;
	uint16_t KpDesolder, KiDesolder, KdDesolder;
} ServiceSettings_t;

/* Полная структура EEPROM */
typedef struct __attribute__((packed)) {
	uint16_t magic;
	uint16_t version;
	TempSettings_t tempSettings;
	ServiceSettings_t serviceSettings;
	uint16_t crc16;
} EEPROM_Config_t;

/* Счётчики сна */
typedef struct {
	uint16_t counterSolder;
	uint16_t counterDesolder;
	bool activeSolder;
	bool activeDesolder;
} SleepCounters_t;

/* Dirty flags для инкрементальной записи */
typedef union {
	struct {
		uint32_t preSet1Solder :1;
		uint32_t preSet2Solder :1;
		uint32_t preSet3Solder :1;
		uint32_t targetSetSolder :1;
		uint32_t preSet1Desolder :1;
		uint32_t preSet2Desolder :1;
		uint32_t preSet3Desolder :1;
		uint32_t targetSetDesolder :1;

		uint32_t flags :1;
		uint32_t sleepTempSolder :1;
		uint32_t sleepTempDesolder :1;
		uint32_t slopeSolder :1;
		uint32_t biasSolder :1;
		uint32_t slopeDesolder :1;
		uint32_t biasDesolder :1;
		uint32_t preSleepTimeoutSolder :1;
		uint32_t sleepTimeoutSolder :1;

		uint32_t preSleepTimeoutDesolder :1;
		uint32_t sleepTimeoutDesolder :1;
		uint32_t KpSolder :1;
		uint32_t KiSolder :1;
		uint32_t KdSolder :1;
		uint32_t KpDesolder :1;
		uint32_t KiDesolder :1;
		uint32_t KdDesolder :1;

		uint32_t reserved :7;
	};
	uint32_t all;
} DirtyFlags_t;

/* =======================================================================
 ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
 ======================================================================= */

extern volatile WorkFlags_t g_WorkFlags;
extern TempSettings_t g_TempSettings;
extern ServiceSettings_t g_ServiceSettings;
extern SleepCounters_t g_SleepCounters;
extern volatile DirtyFlags_t g_DirtyFlags;
extern volatile uint16_t g_SaveDelayCounter;

extern uint16_t g_tCurrentSolder;
extern uint16_t g_tCurrentDesolder;

/* =======================================================================
 API
 ======================================================================= */

/* Инициализация и EEPROM */
void CONFIG_Init(void);
bool CONFIG_LoadFromEEPROM(void);
bool CONFIG_SaveToEEPROM(void);
void CONFIG_ValidateAll(void);
bool CONFIG_IsDirty(void);
void CONFIG_SaveServiceSettings(void);

/* Пресеты */
void CONFIG_SetPresetSolder(uint8_t num, uint16_t val);
void CONFIG_SetPresetDesolder(uint8_t num, uint16_t val);
uint16_t CONFIG_GetPresetSolder(uint8_t num);
uint16_t CONFIG_GetPresetDesolder(uint8_t num);

/* Целевые температуры */
void CONFIG_SetTargetSolder(uint16_t val);
void CONFIG_SetTargetDesolder(uint16_t val);
uint16_t CONFIG_GetEffectiveTargetSolder(void);
uint16_t CONFIG_GetEffectiveTargetDesolder(void);

/* Сервисные настройки */
void CONFIG_SetSleepTempSolder(uint16_t val);
void CONFIG_SetSleepTempDesolder(uint16_t val);
void CONFIG_SetSlopeSolder(uint16_t val);
void CONFIG_SetBiasSolder(uint16_t val);
void CONFIG_SetSlopeDesolder(uint16_t val);
void CONFIG_SetBiasDesolder(uint16_t val);
void CONFIG_SetPreSleepTimeoutSolder(uint16_t min);
void CONFIG_SetSleepTimeoutSolder(uint16_t min);
void CONFIG_SetPreSleepTimeoutDesolder(uint16_t min);
void CONFIG_SetSleepTimeoutDesolder(uint16_t min);
void CONFIG_SetKpSolderer(uint16_t val);
void CONFIG_SetKiSolderer(uint16_t val);
void CONFIG_SetKdSolderer(uint16_t val);
void CONFIG_SetKpDesolder(uint16_t val);
void CONFIG_SetKiDesolder(uint16_t val);
void CONFIG_SetKdDesolder(uint16_t val);

void CONFIG_SetSleepEnabledSolder(bool en);
void CONFIG_SetSleepEnabledDesolder(bool en);
void CONFIG_SetBuzzerEnabled(bool en);

/* Счётчики сна */
void CONFIG_ActivateSleepCounterSolder(void);
void CONFIG_DeactivateSleepCounterSolder(void);
void CONFIG_ActivateSleepCounterDesolder(void);
void CONFIG_DeactivateSleepCounterDesolder(void);
void CONFIG_ResetSleepCounterSolder(void);
void CONFIG_ResetSleepCounterDesolder(void);
void CONFIG_ResetSleepCounterToSleep(bool is_solder);  /* Взвод 2й ступени */

/* Callback'и из heater.c — вызываются из CONFIG_DecrementSleepCounters */
void HEATER_OnSleepTickSolder(void);
void HEATER_OnSleepTickDesolder(void);
void CONFIG_DecrementSleepCounters(void);
bool CONFIG_IsSleepCounterActiveSolder(void);
bool CONFIG_IsSleepCounterActiveDesolder(void);

/* Сброс к заводским настройкам */
void CONFIG_ResetToDefaults(bool solder_tool);

/* CRC */
uint16_t CONFIG_CalculateCRC16(const uint8_t *data, uint32_t len);
bool CONFIG_VerifyCRC(void);

/* Утилиты */
static inline uint16_t CONFIG_MinutesToTicks(uint16_t min) {
	return min * 2;
}
static inline uint16_t CONFIG_TicksToMinutes(uint16_t t) {
	return t / 2;
}
static inline uint16_t CONFIG_Clamp(int32_t v, int32_t mn, int32_t mx) {
	return (v < mn) ? mn : (v > mx) ? mx : (uint16_t) v;
}

#ifdef __cplusplus
}
#endif
#endif /* CONFIG_H */
