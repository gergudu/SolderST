/**
 * @file commands.h
 * @brief Обработка команд и кнопок с ускорением для рабочего и сервисного меню
 */

#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdint.h>
#include <stdbool.h>
#include "buttons.h"

/* ========================================================================== */
/* Публичные функции                                                          */
/* ========================================================================== */

/**
 * @brief Обработчик события кнопки
 * @param event Тип события (BTN_EVENT_*)
 */
void COMMANDS_HandleButtonEvent(ButtonEvent_t event);

/**
 * @brief Переключение инструмента (SOLD/VAC) на главном экране
 */
void COMMANDS_ToggleTool(void);

/**
 * @brief Включение/выключение питания на главном экране
 */
void COMMANDS_TogglePower(void);

/**
 * @brief Вход/выход в сервисное меню
 */
void COMMANDS_ToggleServiceMode(void);

/**
 * @brief Применяет пресет температуры (1..3)
 */
void COMMANDS_ApplyPreset(uint8_t preset_num);

/**
 * @brief Сохраняет текущую температуру в пресет (1..3)
 */
void COMMANDS_SaveToPreset(uint8_t preset_num);

#endif // COMMANDS_H
