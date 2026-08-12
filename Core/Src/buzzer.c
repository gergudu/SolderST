#include "buzzer.h"
#include "main.h"

/* Тиков TIM5 (100 Гц, 10 мс/тик) до автовыключения. 0 = зуммер молчит. */
static volatile uint16_t s_ticksLeft = 0;

void BUZZER_Beep(uint16_t duration_ms) {
    uint16_t ticks = duration_ms / 10;
    if (ticks == 0) ticks = 1;

    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
    s_ticksLeft = ticks; /* реcтарт отсчёта, а не сложение */
}

void BUZZER_Tick(void) {
    if (s_ticksLeft == 0) return;

    s_ticksLeft--;
    if (s_ticksLeft == 0) {
        HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
    }
}
