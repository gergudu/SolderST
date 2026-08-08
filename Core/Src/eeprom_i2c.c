#include "eeprom_i2c.h"

/* ================= Private ================= */

static EEPROM_Status_t EEPROM_I2C_WaitReady(void);

/* ================= Implementation ================= */

EEPROM_Status_t EEPROM_I2C_Init(void)
{
    return EEPROM_I2C_IsReady();
}

EEPROM_Status_t EEPROM_I2C_IsReady(void)
{
    if (HAL_I2C_IsDeviceReady(&hi2c1,
                              EEPROM_I2C_ADDRESS,
                              3,
                              10) == HAL_OK)
    {
        return EEPROM_OK;
    }
    return EEPROM_ERROR;
}

/* ACK polling without fixed delay */
static EEPROM_Status_t EEPROM_I2C_WaitReady(void)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < EEPROM_READY_TIMEOUT_MS)
    {
        if (HAL_I2C_IsDeviceReady(&hi2c1,
                                  EEPROM_I2C_ADDRESS,
                                  1,
                                  5) == HAL_OK)
        {
            return EEPROM_OK;
        }
    }
    return EEPROM_TIMEOUT;
}

/* ================= Read ================= */

EEPROM_Status_t EEPROM_I2C_ReadByte(uint16_t address, uint8_t *data)
{
    if (!data || address >= EEPROM_SIZE)
        return EEPROM_ERROR;

    uint8_t addr = (uint8_t)address;

    if (HAL_I2C_Master_Transmit(&hi2c1,
                                EEPROM_I2C_ADDRESS,
                                &addr,
                                1,
                                100) != HAL_OK)
        return EEPROM_ERROR;

    if (HAL_I2C_Master_Receive(&hi2c1,
                               EEPROM_I2C_ADDRESS,
                               data,
                               1,
                               100) != HAL_OK)
        return EEPROM_ERROR;

    return EEPROM_OK;
}

EEPROM_Status_t EEPROM_I2C_ReadBuffer(uint16_t address,
                                      uint8_t *buffer,
                                      uint16_t length)
{
    if (!buffer || length == 0)
        return EEPROM_ERROR;

    if ((address + length) > EEPROM_SIZE)
        return EEPROM_ERROR;

    uint8_t addr = (uint8_t)address;

    if (HAL_I2C_Master_Transmit(&hi2c1,
                                EEPROM_I2C_ADDRESS,
                                &addr,
                                1,
                                100) != HAL_OK)
        return EEPROM_ERROR;

    if (HAL_I2C_Master_Receive(&hi2c1,
                               EEPROM_I2C_ADDRESS,
                               buffer,
                               length,
                               100) != HAL_OK)
        return EEPROM_ERROR;

    return EEPROM_OK;
}

/* ================= Write ================= */

EEPROM_Status_t EEPROM_I2C_WriteByte(uint16_t address, uint8_t data)
{
    if (address >= EEPROM_SIZE)
        return EEPROM_ERROR;

    uint8_t buf[2];
    buf[0] = (uint8_t)address;
    buf[1] = data;

    if (HAL_I2C_Master_Transmit(&hi2c1,
                                EEPROM_I2C_ADDRESS,
                                buf,
                                2,
                                100) != HAL_OK)
        return EEPROM_ERROR;

    return EEPROM_I2C_WaitReady();
}

/*
 * Page-aware write
 * Supports any EEPROM_PAGE_SIZE >= 2
 */
EEPROM_Status_t EEPROM_I2C_WriteBuffer(uint16_t address,
                                       const uint8_t *buffer,
                                       uint16_t length)
{
    if (!buffer || length == 0)
        return EEPROM_ERROR;

    if ((address + length) > EEPROM_SIZE)
        return EEPROM_ERROR;

    uint16_t offset = 0;

    while (offset < length)
    {
        uint16_t cur_addr   = address + offset;
        uint16_t page_off   = cur_addr % EEPROM_PAGE_SIZE;
        uint16_t page_space = EEPROM_PAGE_SIZE - page_off;
        uint16_t chunk      = length - offset;

        if (chunk > page_space)
            chunk = page_space;

        /* address + data */
        uint8_t tx[EEPROM_PAGE_SIZE + 1];
        tx[0] = (uint8_t)cur_addr;

        for (uint16_t i = 0; i < chunk; i++)
            tx[i + 1] = buffer[offset + i];

        if (HAL_I2C_Master_Transmit(&hi2c1,
                                    EEPROM_I2C_ADDRESS,
                                    tx,
                                    chunk + 1,
                                    100) != HAL_OK)
            return EEPROM_ERROR;

        if (EEPROM_I2C_WaitReady() != EEPROM_OK)
            return EEPROM_TIMEOUT;

        offset += chunk;
    }

    return EEPROM_OK;
}

/* ================= Erase ================= */

EEPROM_Status_t EEPROM_I2C_Erase(uint8_t fill_value)
{
    uint8_t page[EEPROM_PAGE_SIZE];

    for (uint16_t i = 0; i < EEPROM_PAGE_SIZE; i++)
        page[i] = fill_value;

    for (uint16_t addr = 0; addr < EEPROM_SIZE; addr += EEPROM_PAGE_SIZE)
    {
        if (EEPROM_I2C_WriteBuffer(addr, page, EEPROM_PAGE_SIZE) != EEPROM_OK)
            return EEPROM_ERROR;
    }

    return EEPROM_OK;
}

/* ================= Compatible API ================= */

/* Залипает в true при первой неудачной операции чтения/записи и НЕ
   сбрасывается программно (по требованию — до перезагрузки). Единая
   точка входа для обеих обёрток ниже, которыми пользуется весь
   остальной код (config.c и т.д.). */
volatile bool g_EepromFault = false;

bool EEPROM_I2C_Read(uint16_t address, uint8_t *data, uint16_t size)
{
    bool ok = (EEPROM_I2C_ReadBuffer(address, data, size) == EEPROM_OK);
    if (!ok) g_EepromFault = true;
    return ok;
}

bool EEPROM_I2C_Write(uint16_t address, uint8_t *data, uint16_t size)
{
    /* Приводим const uint8_t* к uint8_t* для совместимости */
    bool ok = (EEPROM_I2C_WriteBuffer(address, (const uint8_t*)data, size) == EEPROM_OK);
    if (!ok) g_EepromFault = true;
    return ok;
}

/* Сырой код возврата последнего HAL_I2C_IsDeviceReady — временная
   диагностика, см. UI_DrawInfoZone. 0=HAL_OK, 1=HAL_ERROR, 2=HAL_BUSY,
   3=HAL_TIMEOUT (см. enum HAL_StatusTypeDef). */
volatile uint8_t g_EepromDebugHalStatus = 99;

/**
 * @brief Прямая проверка присутствия микросхемы на шине (ACK на адрес,
 *        HAL_I2C_IsDeviceReady) — без чтения/записи каких-либо данных
 *        и без интерпретации их содержимого (magic/CRC). Самый прямой
 *        из возможных способов детектировать физическое отсутствие
 *        EEPROM. При неудаче сразу взводит g_EepromFault.
 * @retval true  — устройство ответило на шине
 * @retval false — устройство не отвечает (не подключено/неисправно)
 */
bool EEPROM_I2C_IsPresent(void)
{
    /* Временно: вызываем HAL напрямую и сохраняем сырой код возврата
       для отображения на экране — чтобы увидеть, что реально происходит
       на шине, а не полагаться на уже готовую обёртку. */
    HAL_StatusTypeDef st = HAL_I2C_IsDeviceReady(&hi2c1, EEPROM_I2C_ADDRESS, 3, 100);
    g_EepromDebugHalStatus = (uint8_t)st;

    bool ok = (st == HAL_OK);
    if (!ok) g_EepromFault = true;
    return ok;
}
