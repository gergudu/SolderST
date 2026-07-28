#ifndef ILI9341_H
#define ILI9341_H

#include <stdint.h>
#include <stdbool.h>
#include "display.h"

/* Физические параметры панели (landscape 320x240) */
#define ILI9341_WIDTH   320
#define ILI9341_HEIGHT  240

/* Команды */
#define ILI9341_SWRESET     0x01
#define ILI9341_SLPOUT      0x11
#define ILI9341_INVOFF      0x20
#define ILI9341_INVON       0x21
#define ILI9341_DISPOFF     0x28
#define ILI9341_DISPON      0x29
#define ILI9341_CASET       0x2A
#define ILI9341_RASET       0x2B
#define ILI9341_RAMWR       0x2C
#define ILI9341_MADCTL      0x36
#define ILI9341_COLMOD      0x3A
#define ILI9341_FRMCTR1     0xB1
#define ILI9341_DFUNCTR     0xB6
#define ILI9341_PWCTR1      0xC0
#define ILI9341_PWCTR2      0xC1
#define ILI9341_VMCTR1      0xC5
#define ILI9341_VMCTR2      0xC7
#define ILI9341_PGAMCTRL    0xE0
#define ILI9341_NGAMCTRL    0xE1

/* MADCTL для landscape 320x240 */
#define ILI9341_MADCTL_LANDSCAPE  0x28   /* MX+BGR */

/* Публичный API */
void ILI9341_Init(void);
bool ILI9341_IsBusy(void);
void ILI9341_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ILI9341_FillScreen(uint16_t color);
void ILI9341_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void ILI9341_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
void ILI9341_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ILI9341_DrawCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);
void ILI9341_FillCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);
bool ILI9341_BlitLineAsync(uint16_t x, uint16_t y, uint16_t w, const uint16_t *pixels);
bool ILI9341_FillRectAsync(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/* Глобальный объект интерфейса для DISPLAY_RegisterDriver */
extern const Display_Driver_t ili9341_interface;

#endif /* ILI9341_H */
