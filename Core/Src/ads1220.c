/**
 * @file    ads1220.c
 * @brief   Драйвер двух ADS1220 для измерения RTD паяльника и отсоса.
 *
 * Два отдельных чипа на SPI2:
 *   Паяльник:  CS = ADS1220_Solder_CS  (PB8)
 *   Отсос:     CS = ADS1220_Desolder_CS (PB9)
 *
 * DRDY/DOUT совмещены — готовность данных определяется опросом MISO
 * (уровень LOW на MISO при деассертированном CS = данные готовы).
 * Таймаут ожидания: ADS1220_DRDY_TIMEOUT_MS.
 *
 * Схема (одинакова для обоих каналов): 2-Wire RTD
 *   REG0 = 0x68: AIN0/AIN1, Gain=2, PGA on
 *   REG1 = 0x04: 20 SPS, normal, continuous
 *   REG2 = 0x55: внешняя опора REFP0/REFN0, IDAC=500мкА
 *   REG3 = 0x80: IDAC1→AIN3/REFN1
 */

#include "ads1220.h"
#include "config.h"

/* --------------------------------------------------------------------------
 * CS макросы для каждого чипа
 * -------------------------------------------------------------------------- */
#define CS_SOLDER_LOW()    HAL_GPIO_WritePin(ADS1220_Solder_CS_GPIO_Port,   ADS1220_Solder_CS_Pin,   GPIO_PIN_RESET)
#define CS_SOLDER_HIGH()   HAL_GPIO_WritePin(ADS1220_Solder_CS_GPIO_Port,   ADS1220_Solder_CS_Pin,   GPIO_PIN_SET)
#define CS_DESOLDER_LOW()  HAL_GPIO_WritePin(ADS1220_Desolder_CS_GPIO_Port, ADS1220_Desolder_CS_Pin, GPIO_PIN_RESET)
#define CS_DESOLDER_HIGH() HAL_GPIO_WritePin(ADS1220_Desolder_CS_GPIO_Port, ADS1220_Desolder_CS_Pin, GPIO_PIN_SET)

/* Полная шкала ADS1220: 2^23 - 1 */
#define ADS1220_FULL_SCALE  8388607.0f

/* --------------------------------------------------------------------------
 * Тип канала
 * -------------------------------------------------------------------------- */
typedef enum {
    CH_SOLDER   = 0,
    CH_DESOLDER = 1,
} Channel_t;

/* --------------------------------------------------------------------------
 * Низкоуровневые функции SPI (с выбором CS)
 * -------------------------------------------------------------------------- */

static void cs_low(Channel_t ch) {
    if (ch == CH_SOLDER) CS_SOLDER_LOW();
    else                  CS_DESOLDER_LOW();
}

static void cs_high(Channel_t ch) {
    if (ch == CH_SOLDER) CS_SOLDER_HIGH();
    else                  CS_DESOLDER_HIGH();
}

static bool spi_write_byte(uint8_t byte) {
    return HAL_SPI_Transmit(&hspi2, &byte, 1, 10) == HAL_OK;
}

static bool spi_read_bytes(uint8_t *buf, uint16_t len) {
    return HAL_SPI_Receive(&hspi2, buf, len, 10) == HAL_OK;
}

static bool write_reg(Channel_t ch, uint8_t reg, uint8_t value) {
    cs_low(ch);
    bool ok = spi_write_byte(ADS1220_CMD_WREG(reg));
    if (ok) ok = spi_write_byte(value);
    cs_high(ch);
    return ok;
}

static bool read_reg(Channel_t ch, uint8_t reg, uint8_t *value) {
    cs_low(ch);
    bool ok = spi_write_byte(ADS1220_CMD_RREG(reg));
    if (ok) ok = spi_read_bytes(value, 1);
    cs_high(ch);
    return ok;
}

/* --------------------------------------------------------------------------
 * Ожидание DRDY через опрос MISO (CS деассертирован)
 * ADS1220 держит DOUT/DRDY LOW когда данные готовы
 * -------------------------------------------------------------------------- */
static bool wait_drdy(Channel_t ch) {
    /* Простое ожидание одного периода преобразования (20 SPS = 50 мс) */
    (void)ch;
    HAL_Delay(60);
    return true;
}

/* --------------------------------------------------------------------------
 * Инициализация одного чипа
 * -------------------------------------------------------------------------- */
static bool init_channel(Channel_t ch) {
    cs_high(ch);
    HAL_Delay(5);

    /* Сброс */
    cs_low(ch);
    spi_write_byte(ADS1220_CMD_RESET);
    cs_high(ch);
    HAL_Delay(5);

    if (!write_reg(ch, 0, 0x68)) return false;  /* AIN0/AIN1, Gain=2 */
    if (!write_reg(ch, 1, 0x04)) return false;  /* 20 SPS, continuous */
    if (!write_reg(ch, 2, 0x55)) return false;  /* внешняя опора, IDAC=500мкА */
    if (!write_reg(ch, 3, 0x80)) return false;  /* IDAC1→AIN3/REFN1 */

    HAL_Delay(2);

    /* Старт непрерывных преобразований */
    cs_low(ch);
    spi_write_byte(ADS1220_CMD_START);
    cs_high(ch);

    return true;
}

/* --------------------------------------------------------------------------
 * Чтение сырых данных одного чипа
 * -------------------------------------------------------------------------- */
static bool read_raw(Channel_t ch, int32_t *out_raw) {
    if (!wait_drdy(ch)) return false;

    uint8_t buf[3] = {0};
    cs_low(ch);
    bool ok = spi_write_byte(ADS1220_CMD_RDATA);
    if (ok) ok = spi_read_bytes(buf, 3);
    cs_high(ch);
    if (!ok) return false;

    int32_t raw = ((int32_t)buf[0] << 16)
                | ((int32_t)buf[1] << 8)
                |  (int32_t)buf[2];

    if (raw & 0x800000) raw |= 0xFF000000;  /* знаковое расширение */

    *out_raw = raw;
    return true;
}

/* --------------------------------------------------------------------------
 * Публичные функции
 * -------------------------------------------------------------------------- */

bool ADS1220_InitSolder(void) {
    return init_channel(CH_SOLDER);
}

bool ADS1220_InitDesolder(void) {
    return init_channel(CH_DESOLDER);
}

bool ADS1220_Init(void) {
    bool ok = ADS1220_InitSolder();
    ok &= ADS1220_InitDesolder();
    return ok;
}

bool ADS1220_ReadTempSolder(float *out_temp_c) {
    int32_t raw = 0;
    if (!read_raw(CH_SOLDER, &raw)) return false;

    float r_rtd = ((float)raw / ADS1220_FULL_SCALE) * (ADS1220_RREF_OHM / ADS1220_PGA_GAIN);
    float temp  = (r_rtd - (float)g_ServiceSettings.biasSolder / 10.0f)
                  * 1000.0f / (float)g_ServiceSettings.slopeSolder;
    *out_temp_c = temp;
    return true;
}

bool ADS1220_ReadTempDesolder(float *out_temp_c) {
    int32_t raw = 0;
    if (!read_raw(CH_DESOLDER, &raw)) return false;

    float r_rtd = ((float)raw / ADS1220_FULL_SCALE) * (ADS1220_RREF_OHM / ADS1220_PGA_GAIN);
    float temp  = (r_rtd - (float)g_ServiceSettings.biasDesolder / 10.0f)
                  * 1000.0f / (float)g_ServiceSettings.slopeDesolder;
    *out_temp_c = temp;
    return true;
}

bool ADS1220_ReadRaw(int32_t *out_raw) {
    return read_raw(CH_SOLDER, out_raw);
}

bool ADS1220_IsDataReady(void) {
    /* Простой таймаут вместо DRDY пина */
    return true;
}

bool ADS1220_ReadReg(uint8_t reg, uint8_t *value) {
    return read_reg(CH_SOLDER, reg, value);
}

bool ADS1220_ReadRegDesolder(uint8_t reg, uint8_t *value) {
    return read_reg(CH_DESOLDER, reg, value);
}
