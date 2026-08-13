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
 * DRDY на DOUT не разведён отдельным пином (см. README, раздел
 * "Известные ограничения") — читаем без опроса DRDY, просто не чаще
 * раза в ~55 мс на канал (см. ADS1220_Tick ниже), неблокирующе.
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
#define CMD_WREG(r) (0x40 | ((r) << 2))

/* =========================================================================
 * Конфигурация регистров.
 *
 * ВНИМАНИЕ: значения ниже НЕ менялись при этой правке — комментарии
 * приведены в соответствие с реальными битами (проверено побитово по
 * даташиту TI), но сама конфигурация железа осталась как была,
 * т.к. паяльник эмпирически уже даёт адекватную температуру на
 * реальном приборе. Расхождения с исходными комментариями (MUX
 * реально AIN1+/AIN0-, а не AIN0+/AIN1-; IDAC реально 1000мкА, а не
 * 500мкА) — задокументированы, но НЕ исправлены без сверки со
 * схемой, см. обсуждение в истории коммитов.
 *
 * REG0 = 0x68: MUX=0110 -> AIN1(+)/AIN0(-), GAIN=100 -> x16, PGA вкл.
 *              (ADS1220_PGA_GAIN=16.0f в ads1220.h соответствует
 *              реальным битам; неиспользуемый декоративный макрос
 *              ADS1220_REG0_GAIN_1 в хэдере — неверно назван и не
 *              участвует в сборке REG0_VAL, оставлен как есть)
 * REG1 = 0x04: 20 SPS, нормальный режим, непрерывное преобразование
 *              (совпадает с комментарием и макросами хэдера — здесь
 *              расхождений не было)
 * REG2 = 0x55: VREF=01 -> внешний REFP0/REFN0, IDAC=101 -> 1000 мкА
 *              (не 500мкА, как было в исходном комментарии и в
 *              ADS1220_IDAC_UA хэдера — тот макрос декоративный, в
 *              расчёте не участвует, реальный ток вдвое больше
 *              документированного; на РАСЧЁТ температуры не влияет,
 *              т.к. измерение рациометрическое — ток через Rref и
 *              RTD одинаковый и сокращается в отношении, но это
 *              лишний самонагрев RTD)
 * REG3 = 0x80: IDAC1 -> AIN3 (совпадает с хэдером)
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

/* Период между чтениями одного канала — совпадает с периодом
   преобразования (1/20SPS = 50мс), с небольшим запасом. */
#define READ_PERIOD_MS  55

/* Метки времени последнего успешного чтения — раздельно на канал,
   поэтому солдер/десолдер читаются полностью независимо друг от
   друга, а не по очереди с общей задержкой. */
static uint32_t s_lastReadTickSolder   = 0;
static uint32_t s_lastReadTickDesolder = 0;

/* =========================================================================
 * Чтение сырых данных
 * ========================================================================= */

/**
 * @brief Неблокирующее чтение — раньше здесь стоял HAL_Delay(55),
 *        останавливавший весь главный цикл на время ожидания
 *        преобразования (дважды за проход, солдер+десолдер, то есть
 *        ~110мс/итерацию). DRDY отдельным пином не разведён (шарится
 *        с DOUT, см. README) — вместо опроса DRDY просто не читаем
 *        чаще, чем раз в READ_PERIOD_MS по HAL_GetTick(), для КАЖДОГО
 *        канала отдельно (last_tick — указатель на статик своего
 *        канала). Если время ещё не вышло — возвращаем false, ничего
 *        не блокируя; вызывающая сторона (main.c) просто не получит
 *        новое значение в этом проходе цикла и попробует на следующем.
 */
static bool read_raw(GPIO_TypeDef *port, uint16_t pin, uint32_t *last_tick, int32_t *out) {
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - *last_tick) < READ_PERIOD_MS) return false;
    *last_tick = now; /* обновляем в любом случае — даже при ошибке SPI не
                          долбим шину каждый проход цикла, а ждём следующий период */

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

/* ADS1220_Init() (комбинированная InitSolder+InitDesolder) удалена —
   не вызывалась, main.c зовёт две раздельные функции сам. */

bool ADS1220_ReadTempSolder(float *out_temp_c) {
    int32_t raw = 0;
    if (!read_raw(ADS1220_Solder_CS_GPIO_Port, ADS1220_Solder_CS_Pin,
                  &s_lastReadTickSolder, &raw))
        return false;

    float r_rtd = ((float)raw / FULL_SCALE)
                * (ADS1220_RREF_OHM / ADS1220_PGA_GAIN);
    *out_temp_c = (r_rtd - (float)g_ServiceSettings.biasSolder / 10.0f)
                * 1000.0f / (float)g_ServiceSettings.slopeSolder;
    return true;
}

bool ADS1220_ReadTempDesolder(float *out_temp_c) {
    int32_t raw = 0;
    if (!read_raw(ADS1220_Desolder_CS_GPIO_Port, ADS1220_Desolder_CS_Pin,
                  &s_lastReadTickDesolder, &raw))
        return false;

    float r_rtd = ((float)raw / FULL_SCALE)
                * (ADS1220_RREF_OHM / ADS1220_PGA_GAIN);
    *out_temp_c = (r_rtd - (float)g_ServiceSettings.biasDesolder / 10.0f)
                * 1000.0f / (float)g_ServiceSettings.slopeDesolder;
    return true;
}
