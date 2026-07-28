
#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Инициализация графической оболочки (если нужна начальная заставка)
 */
void UI_Init(void);

/**
 * @brief Главная точка входа UI. Вызывается в каждом проходе main loop.
 * Внутри сама решает, какой экран рисовать, исходя из STATE_GetMode().
 */
void UI_UpdateLoop(void);

/**
 * @brief Принудительная отрисовка заголовка (например, при смене инструмента)
 */
void UI_DrawHeader(void);
void UI_DrawExpertWarn(void);
void UI_DrawExpertMenu(void);

/**
 * @brief Флаг для внешних модулей, сообщающий, что экрану нужна полная очистка
 */
extern bool g_UI_NeedsClear;

#endif /* UI_H */
