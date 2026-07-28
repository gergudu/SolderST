#ifndef ST7789_H
#define ST7789_H

#include <stdint.h>
#include <stdbool.h>
#include "display.h"

/* Физические параметры панели */
#define ST7789_WIDTH   320
#define ST7789_HEIGHT  240
#define Y_OFFSET       0

/* Команды контроллера */
#define SWRESET     0x01
#define SLPOUT      0x11
#define INVOFF      0x20
#define INVON       0x21
#define DISPOFF     0x28
#define DISPON      0x29
#define CASET       0x2A
#define RASET       0x2B
#define RAMWR       0x2C
#define MADCTL      0x36
#define COLMOD      0x3A

/* Расширенные команды */
#define CMD_VCOMS       0xBB
#define CMD_VRHS        0xC3
#define CMD_PWCTRL1     0xD0
#define CMD_PVGAMCTRL   0xE0
#define CMD_NVGAMCTRL   0xE1
#define CMD_RAMCTRL     0xB0

/* Публичный API драйвера */
void ST7789_Init(void);
bool ST7789_IsBusy(void);
void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7789_FillScreen(uint16_t color);
void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void ST7789_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
void ST7789_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7789_DrawCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);
void ST7789_FillCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);

/* DMA функции */
bool ST7789_BlitLineAsync(uint16_t x, uint16_t y, uint16_t w, const uint16_t *pixels);
bool ST7789_FillRectAsync(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/* Глобальный объект интерфейса */
extern const Display_Driver_t st7789_interface;

#endif /* ST7789_H */
