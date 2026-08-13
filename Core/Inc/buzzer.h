#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

/**
 * @file buzzer.h
 * @brief Активный (автогенерирующий) зуммер на BEEP_Pin (PB3, ключ 2N7002
 *        с общим истоком). Драйв — просто GPIO HIGH/LOW, тон генерирует
 *        сам зуммер.
 *
 * Длительность сигнала считается неблокирующе — тиками TIM5 (100 Гц,
 * тик = 10 мс), в том же прерывании, что уже декрементирует
 * g_SaveDelayCounter и антидребезг кнопок. Никаких HAL_Delay().
 */

/**
 * @brief Приоритет сигнала. Новый сигнал перезапускает уже играющий,
 *        только если его приоритет >= приоритета текущего — иначе
 *        запрос молча игнорируется (не встаёт в очередь). Без этого
 *        более важный сигнал мог бы обрываться менее важным, который
 *        запустился чуть позже (реальный случай: PRESLEEP_SLEEP
 *        полного сна почти всегда обрывался POWEROFF, т.к. отключение
 *        питания детектится тиком позже на TIM3).
 */
typedef enum {
    BUZZER_PRIO_PRESLEEP = 0,  /* преслип — самый низкий, просто "к сведению" */
    BUZZER_PRIO_SLEEP,         /* полный сон */
    BUZZER_PRIO_POWEROFF,      /* отключение питания */
    BUZZER_PRIO_FAULT,         /* неисправность — самый высокий */
} BuzzerPriority_t;

/**
 * @brief Запустить сигнал на duration_ms миллисекунд (округляется вниз
 *        до 10 мс, минимум один тик). Эквивалентно
 *        BUZZER_BeepPattern(1, duration_ms, 0, prio).
 */
void BUZZER_Beep(uint16_t duration_ms, BuzzerPriority_t prio);

/**
 * @brief Запустить серию из count импульсов длительностью on_ms с
 *        паузой off_ms между ними (пауза после последнего импульса не
 *        нужна). Запрос с приоритетом ниже уже играющего сигнала
 *        молча игнорируется — текущий сигнал доигрывает до конца.
 */
void BUZZER_BeepPattern(uint8_t count, uint16_t on_ms, uint16_t off_ms, BuzzerPriority_t prio);

/**
 * @brief Тик FSM — вызывается из HAL_TIM_PeriodElapsedCallback(TIM5)
 *        в main.c, 100 Гц. Больше ниоткуда не вызывать.
 */
void BUZZER_Tick(void);

#endif /* BUZZER_H */
