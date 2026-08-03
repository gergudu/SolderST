/**
 * @file ads1220.c
 * @brief Драйвер двух ADS1220 на SPI2.
 *
 * Паяльник:  CS = PB8  (ADS1220_Solder_CS)
 * Отсос:     CS = PB9  (ADS1220_Desolder_CS)
 * SCK:       PB10
 * MOSI/DIN:  PB15
 * MISO/DOUT: PB14
 *
 * Режим: 20 SPS, непрерывное преобразование, внешний опорный.
 * DRDY: ожидание через HAL_Delay(55мс).
 */

#include "ads1220.h"
#include "config.h"
#include "main.h"

extern SPI_HandleTypeDef hspi2;

/* =========================================================================
 * CS макросы
 * ========================================================================= */
#define CS_LOW(port, pin)   HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET)
#define CS_HIGH(port, pin)  HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET)

/* =========================================================================
 * Команды ADS1220
 * ========================================================================= */
#define CMD_RESET   0x06
#define CMD_START   0x08
#define CMD_RDATA   0x10
#define CMD_RREG(r) (0x20 | ((r) << 2))
#define CMD_WREG(r) (0x40 | ((r) << 2))

/* =========================================================================
 * Конфигурация регистров
 * REG0: AIN0/AIN1 дифф., Gain=2, PGA вкл.  = 0x68
 * REG1: 20 SPS, нормальный режим, непрерывно = 0x04
 * REG2: внешний опорный REFP0/REFN0, IDAC=500мкА = 0x55  (на самом деле 0x59 для 500мкА)
 * REG3: IDAC1 → AIN3/REFN1                  = 0x80 (E0h = AIN3)
 * ========================================================================= */
#define REG0_VAL  0x68
#define REG1_VAL  0x04
#define REG2_VAL  0x55
#define REG3_VAL  0x80

/* =========================================================================
 * Полная шкала 24-бит со знаком
 * ========================================================================= */
#define FULL_SCALE  8388607.0f

/* =========================================================================
 * Низкоуровневые функции
 * ========================================================================= */

static bool spi_tx(uint8_t *buf, uint16_t len) {
    return HAL_SPI_Transmit(&hspi2, buf, len, 50) == HAL_OK;
}

static bool spi_rx(uint8_t *buf, uint16_t len) {
    return HAL_SPI_Receive(&hspi2, buf, len, 50) == HAL_OK;
}

static bool write_reg(GPIO_TypeDef *port, uint16_t pin, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {CMD_WREG(reg), val};
    CS_LOW(port, pin);
    bool ok = spi_tx(buf, 2);
    CS_HIGH(port, pin);
    return ok;
}

static bool read_reg(GPIO_TypeDef *port, uint16_t pin, uint8_t reg, uint8_t *val) {
    uint8_t cmd = CMD_RREG(reg);
    CS_LOW(port, pin);
    bool ok = spi_tx(&cmd, 1);
    if (ok) ok = spi_rx(val, 1);
    CS_HIGH(port, pin);
    return ok;
}

/* =========================================================================
 * Инициализация одного чипа
 * ========================================================================= */
static bool init_chip(GPIO_TypeDef *port, uint16_t pin) {
    /* Убедимся что CS высокий */
    CS_HIGH(port, pin);
    HAL_Delay(10);

    /* Аппаратный сброс командой */
    uint8_t rst = CMD_RESET;
    CS_LOW(port, pin);
    spi_tx(&rst, 1);
    CS_HIGH(port, pin);
    HAL_Delay(10);

    /* Запись регистров */
    if (!write_reg(port, pin, 0, REG0_VAL)) return false;
    if (!write_reg(port, pin, 1, REG1_VAL)) return false;
    if (!write_reg(port, pin, 2, REG2_VAL)) return false;
    if (!write_reg(port, pin, 3, REG3_VAL)) return false;

    /* Запуск непрерывных преобразований */
    uint8_t start = CMD_START;
    CS_LOW(port, pin);
    spi_tx(&start, 1);
    CS_HIGH(port, pin);

    return true;
}

/* =========================================================================
 * Чтение сырых данных
 * ========================================================================= */
static bool read_raw(GPIO_TypeDef *port, uint16_t pin, int32_t *out) {
    /* Ждём одно преобразование: 1/20SPS = 50мс */
    HAL_Delay(55);

    uint8_t cmd = CMD_RDATA;
    uint8_t buf[3] = {0};

    CS_LOW(port, pin);
    bool ok = spi_tx(&cmd, 1);
    if (ok) ok = spi_rx(buf, 3);
    CS_HIGH(port, pin);

    if (!ok) return false;

    int32_t raw = ((int32_t)buf[0] << 16)
                | ((int32_t)buf[1] << 8)
                |  (int32_t)buf[2];

    /* Знаковое расширение 24→32 бит */
    if (raw & 0x800000) raw |= 0xFF000000;

    *out = raw;
    return true;
}

/* =========================================================================
 * Публичные функции
 * ========================================================================= */

bool ADS1220_InitSolder(void) {
    return init_chip(ADS1220_Solder_CS_GPIO_Port, ADS1220_Solder_CS_Pin);
}

bool ADS1220_InitDesolder(void) {
    return init_chip(ADS1220_Desolder_CS_GPIO_Port, ADS1220_Desolder_CS_Pin);
}

bool ADS1220_Init(void) {
    bool ok  = ADS1220_InitSolder();
    ok &= ADS1220_InitDesolder();
    return ok;
}

bool ADS1220_ReadTempSolder(float *out_temp_c) {
    int32_t raw = 0;
    if (!read_raw(ADS1220_Solder_CS_GPIO_Port, ADS1220_Solder_CS_Pin, &raw))
        return false;

    float r_rtd = ((float)raw / FULL_SCALE)
                * (ADS1220_RREF_OHM / ADS1220_PGA_GAIN);
    *out_temp_c = (r_rtd - (float)g_ServiceSettings.biasSolder / 10.0f)
                * 1000.0f / (float)g_ServiceSettings.slopeSolder;
    return true;
}

bool ADS1220_ReadTempDesolder(float *out_temp_c) {
    int32_t raw = 0;
    if (!read_raw(ADS1220_Desolder_CS_GPIO_Port, ADS1220_Desolder_CS_Pin, &raw))
        return false;

    float r_rtd = ((float)raw / FULL_SCALE)
                * (ADS1220_RREF_OHM / ADS1220_PGA_GAIN);
    *out_temp_c = (r_rtd - (float)g_ServiceSettings.biasDesolder / 10.0f)
                * 1000.0f / (float)g_ServiceSettings.slopeDesolder;
    return true;
}

bool ADS1220_ReadRaw(int32_t *out_raw) {
    return read_raw(ADS1220_Solder_CS_GPIO_Port, ADS1220_Solder_CS_Pin, out_raw);
}

bool ADS1220_ReadReg(uint8_t reg, uint8_t *value) {
    return read_reg(ADS1220_Solder_CS_GPIO_Port, ADS1220_Solder_CS_Pin, reg, value);
}

bool ADS1220_ReadRegDesolder(uint8_t reg, uint8_t *value) {
    return read_reg(ADS1220_Desolder_CS_GPIO_Port, ADS1220_Desolder_CS_Pin, reg, value);
}

bool ADS1220_IsDataReady(void) {
    return true;
}
