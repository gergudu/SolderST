/* main.c v6.42 — полная версия */
#include "main.h"
#include "dma.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "gpio.h"
#include "st7789.h"
#include "fonts.h"
#include "display.h"
#include "eeprom_i2c.h"
#include "config.h"
#include "buttons.h"
#include "state.h"
#include "commands.h"
#include "ui.h"
#include "heater.h"
#include <stdio.h>
#include <string.h>

void SystemClock_Config(void);

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_SPI1_Init();
    MX_I2C1_Init();
    MX_TIM2_Init();
    MX_TIM3_Init();
    MX_TIM5_Init();
    MX_SPI2_Init();
    MX_TIM10_Init();

    CONFIG_Init();
    ST7789_Init();
    DISPLAY_RegisterDriver(&st7789_interface);
    /* ADS1220_Init(); */   /* Не запаян — пропускаем */
    HEATER_Init();

    HAL_TIM_Base_Start_IT(&htim5);   /* 10 мс — кнопки */
    HAL_TIM_Base_Start_IT(&htim10);  /* 30 с  — таймеры сна */

    DISPLAY_FillScreen(BLACK);
    DISPLAY_Print(10, 5, "rev 6.42", &AntiquaB_24_uni, YELLOW, BLACK);

    while (1) {

        /* -------------------------------------------------------------------
         * Насос: PB13 = кнопка PB12 (удержание = работает)
         * Передний фронт — сброс таймера сна отсоса
         * ------------------------------------------------------------------- */
        {
            static bool vac_prev = false;
            bool vac = (HAL_GPIO_ReadPin(Btn_Pump_GPIO_Port, Btn_Pump_Pin) == GPIO_PIN_RESET);
            if (vac && !vac_prev) {
                HEATER_ResetSleepDesolder();
            }
            vac_prev = vac;
            HAL_GPIO_WritePin(Pump_On_GPIO_Port, Pump_On_Pin,
                              vac ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }

        /* -------------------------------------------------------------------
         * Индикатор записи EEPROM — кружок в верхнем левом углу дисплея
         * ------------------------------------------------------------------- */
        {
            static bool dot_prev = false;
            bool dot_on = (g_SaveDelayCounter == 0 && CONFIG_IsDirty());
            if (dot_on != dot_prev) {
                DISPLAY_FillCircle(8, 8, 6, dot_on ? WHITE : BLACK);
                dot_prev = dot_on;
            }
        }

        /* -------------------------------------------------------------------
         * FSM кнопок
         * ------------------------------------------------------------------- */
        {
            static uint16_t last_mask = 0;
            ButtonEvent_t event = BUTTONS_Process();

            if (event != BTN_EVENT_NONE) {
                COMMANDS_HandleButtonEvent(event);
                if (CONFIG_IsDirty()) {
                    g_SaveDelayCounter = 0;
                }
            }

            if (g_ButtonContext.stable_mask == 0 && last_mask != 0) {
                if (CONFIG_IsDirty()) {
                    g_SaveDelayCounter = SAVE_DELAY_TICKS;
                }
            }
            last_mask = g_ButtonContext.stable_mask;

            if (g_SaveDelayCounter == 0 && g_ButtonContext.stable_mask == 0
                    && CONFIG_IsDirty()) {
                CONFIG_SaveToEEPROM();
            }
        }

        UI_UpdateLoop();

        HAL_Delay(5);
    }
}

/* USER CODE BEGIN 4 */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM2) {
        HAL_TIM_Base_Stop_IT(&htim2);
        g_WorkFlags.IsMove = false;
        EXTI->IMR |= Dock_Pin;
    } else if (htim->Instance == TIM3) {
        HEATER_Tick();
    } else if (htim->Instance == TIM5) {
        static uint16_t last_gpio  = 0;
        static uint8_t  stable_cnt = 0;

        if (g_SaveDelayCounter) g_SaveDelayCounter--;

        uint16_t gpio = (~GPIOA->IDR) & BTN_ALL_PINS;

        if (gpio == last_gpio) {
            if (stable_cnt < 3) stable_cnt++;
        } else {
            stable_cnt = 1;
            last_gpio  = gpio;
        }

        if (stable_cnt == 3) {
            if (gpio != 0 && g_ButtonContext.stable_mask == 0)
                g_ButtonContext.chord_window = CHORD_WINDOW_TICKS;
            g_ButtonContext.stable_mask = gpio;
        }

        if (g_ButtonContext.stable_mask != 0) {
            g_ButtonContext.buttons_tick++;
            if (g_ButtonContext.chord_window) g_ButtonContext.chord_window--;
        } else {
            g_ButtonContext.buttons_tick = 0;
        }
    } else if (htim->Instance == TIM10) {
        CONFIG_DecrementSleepCounters();
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == Dock_Pin) {
        EXTI->IMR &= ~Dock_Pin;
        g_WorkFlags.IsMove = true;
        HAL_TIM_Base_Start_IT(&htim2);
        HEATER_ResetSleepSolder();
    }
}

/* USER CODE END 4 */

void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 25;
    RCC_OscInitStruct.PLL.PLLN       = 192;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ       = 4;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK) Error_Handler();
}

void Error_Handler(void) {
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif
