#ifndef EEPROM_I2C_H
#define EEPROM_I2C_H

#include "main.h"
#include "i2c.h"
#include <stdbool.h>

/* ================= EEPROM configuration ================= */

/* 24C02 base address = 0x50 (A2..A0 = 0) */
#define EEPROM_I2C_ADDRESS      (0x50 << 1)

/* EEPROM geometry */
#define EEPROM_SIZE             256     // bytes
#define EEPROM_PAGE_SIZE        8       // bytes (8/16/32/64 supported)

/* Timing */
#define EEPROM_READY_TIMEOUT_MS 20

/* ================= Status ================= */

typedef enum {
    EEPROM_OK = 0,
    EEPROM_ERROR,
    EEPROM_TIMEOUT
} EEPROM_Status_t;

/* Неисправность EEPROM: выставляется при первой неудачной операции
   чтения/записи через EEPROM_I2C_Read/Write и залипает до перезагрузки
   (программно нигде не сбрасывается). См. UI_DrawInfoZone. */
extern volatile bool g_EepromFault;

/* ================= API ================= */

EEPROM_Status_t EEPROM_I2C_Init(void);
EEPROM_Status_t EEPROM_I2C_IsReady(void);

EEPROM_Status_t EEPROM_I2C_ReadByte(uint16_t address, uint8_t *data);
EEPROM_Status_t EEPROM_I2C_WriteByte(uint16_t address, uint8_t data);

EEPROM_Status_t EEPROM_I2C_ReadBuffer(uint16_t address,
                                      uint8_t *buffer,
                                      uint16_t length);

EEPROM_Status_t EEPROM_I2C_WriteBuffer(uint16_t address,
                                       const uint8_t *buffer,
                                       uint16_t length);

EEPROM_Status_t EEPROM_I2C_Erase(uint8_t fill_value);

/* ================= Compatible API for config.c ================= */

/**
 * @brief Чтение данных (совместимость с config.c)
 * @param address Адрес в EEPROM
 * @param data Буфер для данных
 * @param size Размер данных
 * @return true если успешно, false если ошибка
 */
bool EEPROM_I2C_Read(uint16_t address, uint8_t *data, uint16_t size);

/**
 * @brief Запись данных (совместимость с config.c)
 * @param address Адрес в EEPROM
 * @param data Буфер с данными
 * @param size Размер данных
 * @return true если успешно, false если ошибка
 */
bool EEPROM_I2C_Write(uint16_t address, uint8_t *data, uint16_t size);

/* Прямая проверка присутствия микросхемы на шине (без чтения/записи
   данных) — см. eeprom_i2c.c. Вызывается один раз в CONFIG_Init(). */
bool EEPROM_I2C_IsPresent(void);

#endif /* EEPROM_I2C_H */
