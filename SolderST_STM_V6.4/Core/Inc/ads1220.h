/**
 ******************************************************************************
 * @file    ads1220.h
 * @brief   Драйвер ADS1220 — 24-bit SPI ADC для измерения RTD паяльника.
 *
 * Схема подключения:
 *   AIN0(+) / AIN1(-) — дифференциальное измерение напряжения на RTD
 *   IDAC1 → AIN0 — ток возбуждения 1000 мкА течёт через Rref(100 Ом) и RTD
 *   REFP0/REFN0    — внешняя опора на резисторе Rref (ratiometric)
 *   CS  → PB8 (SPI2_CS1)
 *   DRDY→ PB9 (вход, активный низкий)
 *
 * RTD паяльника: R = 21.7 + T * 0.072 Ом
 * Калибровка смещения проводов (~0.25 Ом) — через biasSolder в config.
 ******************************************************************************
 */

#ifndef INC_ADS1220_H_
#define INC_ADS1220_H_

#include "main.h"
#include "spi.h"
#include <stdint.h>
#include <stdbool.h>

/* --------------------------------------------------------------------------
 * Параметры измерительной схемы
 * -------------------------------------------------------------------------- */
#define ADS1220_RREF_OHM        1000.0f   /* Опорный резистор, Ом           */
#define ADS1220_IDAC_UA         500       /* Ток возбуждения IDAC1, мкА     */
#define ADS1220_PGA_GAIN        16.0f      /* Коэффициент PGA                */

/* --------------------------------------------------------------------------
 * Команды SPI
 * -------------------------------------------------------------------------- */
#define ADS1220_CMD_RESET       0x06
#define ADS1220_CMD_START       0x08
#define ADS1220_CMD_POWERDOWN   0x02
#define ADS1220_CMD_RDATA       0x10
#define ADS1220_CMD_RREG(r)     (0x20 | ((r) << 2))  /* Читать регистр r      */
#define ADS1220_CMD_WREG(r)     (0x40 | ((r) << 2))  /* Писать регистр r      */

/* --------------------------------------------------------------------------
 * Регистры (адреса 0..3)
 * -------------------------------------------------------------------------- */
/* REG0: MUX[7:4] GAIN[3:1] PGA_BYPASS[0] */
#define ADS1220_REG0_MUX_AIN0_AIN1  (0x00 << 4)  /* AIN0(+)/AIN1(-) */
#define ADS1220_REG0_GAIN_1         (0x00 << 1)  /* PGA gain = 1    */
#define ADS1220_REG0_PGA_BYPASS_OFF (0x00)        /* PGA включён     */

/* REG1: DR[7:5] MODE[4:3] CM[2] TS[1] BCS[0] */
#define ADS1220_REG1_DR_20SPS       (0x00 << 5)  /* 20 SPS           */
#define ADS1220_REG1_MODE_NORMAL    (0x00 << 3)  /* Normal mode      */
#define ADS1220_REG1_CM_CONTINUOUS  (0x01 << 2)  /* Continuous conv  */
#define ADS1220_REG1_TS_OFF         (0x00 << 1)
#define ADS1220_REG1_BCS_OFF        (0x00)

/* REG2: VREF[7:6] 50/60[5:4] PSW[3] IDAC[2:0] */
#define ADS1220_REG2_VREF_REFP0     (0x02 << 6)  /* Внешняя опора REFP0/REFN0 */
#define ADS1220_REG2_FIR_OFF        (0x00 << 4)
#define ADS1220_REG2_PSW_OFF        (0x00 << 3)
#define ADS1220_REG2_IDAC_500UA     (0x05) /* IDAC = 500 мкА */

/* REG3: I1MUX[7:5] I2MUX[4:2] DRDYM[1] RESERVED[0] */
#define ADS1220_REG3_IDAC1_AIN3_REFN1   (0x04 << 5) /* IDAC1 → AIN3 / REFN1 */
#define ADS1220_REG3_IDAC2_OFF      (0x00 << 2)  /* IDAC2 выкл    */
#define ADS1220_REG3_DRDYM_DOUT     (0x00 << 1)  /* DRDY на DOUT  */

/* --------------------------------------------------------------------------
 * Таймаут ожидания DRDY (мс)
 * -------------------------------------------------------------------------- */
#define ADS1220_DRDY_TIMEOUT_MS     200

/* --------------------------------------------------------------------------
 * Публичный API
 * -------------------------------------------------------------------------- */

/**
 * @brief  Инициализация ADS1220: сброс, конфигурация регистров, старт.
 * @retval true  — успешно
 * @retval false — ошибка SPI или чип не отвечает
 */
bool ADS1220_Init(void);

/**
 * @brief  Считать сырое 24-битное значение (знаковое, дополнение до 2).
 * @param  out_raw — указатель для результата
 * @retval true  — данные готовы и прочитаны
 * @retval false — таймаут DRDY или ошибка SPI
 */
bool ADS1220_ReadRaw(int32_t *out_raw);

/**
 * @brief  Получить температуру паяльника в °C.
 *         Вычисление: R_rtd = raw/FS * Rref  →  T = (R_rtd - R0) / coeff
 * @param  out_temp_c — указатель для результата
 * @retval true  — успешно
 * @retval false — таймаут DRDY или ошибка SPI
 */
bool ADS1220_ReadTempSolder(float *out_temp_c);

/**
 * @brief Измерение температуры отсоса (AIN2/AIN3).
 *        Переключает MUX и ждёт одно преобразование (55 мс).
 */
bool ADS1220_ReadTempDesolder(float *out_temp_c);

/**
 * @brief  Проверить флаг DRDY без блокировки.
 * @retval true — данные готовы (DRDY = LOW)
 */
bool ADS1220_IsDataReady(void);
bool ADS1220_ReadReg(uint8_t reg, uint8_t *value);


#endif /* INC_ADS1220_H_ */
