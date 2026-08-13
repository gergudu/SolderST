#include "buzzer.h"
#include "main.h"
#include <stdbool.h>

/* Тиков TIM5 (100 Гц, 10 мс/тик) на "звук"/"паузу" текущего паттерна. */
static volatile uint16_t s_onTicks   = 0;
static volatile uint16_t s_offTicks  = 0;
static volatile uint8_t  s_pulsesLeft = 0;  /* сколько импульсов ещё не отзвучало, включая текущий */
static volatile bool     s_isOnPhase  = false;
static volatile uint16_t s_phaseTicksLeft = 0;
static volatile BuzzerPriority_t s_curPrio = BUZZER_PRIO_PRESLEEP;

static inline uint16_t MsToTicks(uint16_t ms) {
    uint16_t t = ms / 10;
    return t;
}

void BUZZER_BeepPattern(uint8_t count, uint16_t on_ms, uint16_t off_ms, BuzzerPriority_t prio) {
    if (count == 0) return;

    /* Сигнал ниже приоритетом, чем уже играющий — игнорируем, пусть
       текущий доиграет. Не сравниваем строго '>' — сигнал с тем же
       приоритетом тоже перезапускается (например, повторная
       неисправность на другом канале должна снова начать 5 писков). */
    if (s_pulsesLeft > 0 && prio < s_curPrio) return;

    uint16_t onT = MsToTicks(on_ms);
    if (onT == 0) onT = 1;

    s_onTicks    = onT;
    s_offTicks   = MsToTicks(off_ms);
    s_pulsesLeft = count;
    s_isOnPhase  = true;
    s_phaseTicksLeft = onT;
    s_curPrio    = prio;

    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
}

void BUZZER_Beep(uint16_t duration_ms, BuzzerPriority_t prio) {
    BUZZER_BeepPattern(1, duration_ms, 0, prio);
}

void BUZZER_Tick(void) {
    if (s_pulsesLeft == 0) return;

    if (s_phaseTicksLeft > 0) {
        s_phaseTicksLeft--;
        return;
    }

    if (s_isOnPhase) {
        /* Импульс закончился */
        HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
        s_pulsesLeft--;
        if (s_pulsesLeft == 0) return; /* весь паттерн отыгран, приоритет освобождается сам собой */

        if (s_offTicks == 0) {
            /* Без паузы — сразу следующий импульс */
            s_isOnPhase = true;
            s_phaseTicksLeft = s_onTicks;
            HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
        } else {
            s_isOnPhase = false;
            s_phaseTicksLeft = s_offTicks;
        }
    } else {
        /* Пауза закончилась — следующий импульс */
        s_isOnPhase = true;
        s_phaseTicksLeft = s_onTicks;
        HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
    }
}
