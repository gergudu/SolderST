/**
 * @file ili9341.c
 * @brief Драйвер ILI9341 320x240 (landscape).
 *        Те же пины что ST7789: SPI1, DC=PA3, RST=PA4.
 *        DMA-передача строк — идентична ST7789.
 */

#include "ili9341.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdlib.h>

extern SPI_HandleTypeDef hspi1;

/* Пины — те же что у ST7789 */
#define DC_PORT   GPIOA
#define DC_PIN    GPIO_PIN_3
#define RST_PORT  GPIOA
#define RST_PIN   GPIO_PIN_4

static inline void DC_Command(void) { HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_RESET); }
static inline void DC_Data(void)    { HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_SET); }

static void ILI9341_WriteCmd(uint8_t cmd) {
    DC_Command();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, HAL_MAX_DELAY);
}

static void ILI9341_WriteDataByte(uint8_t data) {
    DC_Data();
    HAL_SPI_Transmit(&hspi1, &data, 1, HAL_MAX_DELAY);
}

static void ILI9341_WriteData(const uint8_t *data, uint16_t len) {
    DC_Data();
    HAL_SPI_Transmit(&hspi1, (uint8_t *)data, len, HAL_MAX_DELAY);
}

static void ILI9341_Reset(void) {
    HAL_GPIO_WritePin(RST_PORT, RST_PIN, GPIO_PIN_SET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(RST_PORT, RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(50);   /* как в ST7789 */
    HAL_GPIO_WritePin(RST_PORT, RST_PIN, GPIO_PIN_SET);
    HAL_Delay(150);
}

static void ILI9341_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t d[4];

    ILI9341_WriteCmd(ILI9341_CASET);
    d[0] = x0 >> 8; d[1] = x0 & 0xFF;
    d[2] = x1 >> 8; d[3] = x1 & 0xFF;
    ILI9341_WriteData(d, 4);

    ILI9341_WriteCmd(ILI9341_RASET);
    d[0] = y0 >> 8; d[1] = y0 & 0xFF;
    d[2] = y1 >> 8; d[3] = y1 & 0xFF;
    ILI9341_WriteData(d, 4);

    ILI9341_WriteCmd(ILI9341_RAMWR);
}

/* =========================================================================
 * Инициализация
 * ========================================================================= */

static void ILI9341_InitCommands(void) {
    /* Гарантированный выход из любого состояния —
     * CS приземлён, дисплей мог получить мусор при старте МК */
    ILI9341_WriteCmd(0x28);          /* Display OFF */
    HAL_Delay(20);
    ILI9341_WriteCmd(ILI9341_SWRESET);
    HAL_Delay(200);
    ILI9341_WriteCmd(ILI9341_SWRESET); /* второй раз для надёжности */
    HAL_Delay(200);

    ILI9341_WriteCmd(ILI9341_SLPOUT);  HAL_Delay(120);

    ILI9341_WriteCmd(ILI9341_PWCTR1); ILI9341_WriteDataByte(0x23);
    ILI9341_WriteCmd(ILI9341_PWCTR2); ILI9341_WriteDataByte(0x10);

    ILI9341_WriteCmd(ILI9341_VMCTR1);
    const uint8_t vcom1[] = {0x3E, 0x28};
    ILI9341_WriteData(vcom1, 2);

    ILI9341_WriteCmd(ILI9341_VMCTR2); ILI9341_WriteDataByte(0x86);

    ILI9341_WriteCmd(ILI9341_MADCTL);
    ILI9341_WriteDataByte(ILI9341_MADCTL_LANDSCAPE);

    ILI9341_WriteCmd(ILI9341_COLMOD); ILI9341_WriteDataByte(0x55);

    ILI9341_WriteCmd(ILI9341_FRMCTR1);
    const uint8_t frmctr[] = {0x00, 0x18};
    ILI9341_WriteData(frmctr, 2);

    ILI9341_WriteCmd(ILI9341_DFUNCTR);
    const uint8_t dfunc[] = {0x08, 0x82, 0x27};
    ILI9341_WriteData(dfunc, 3);

    ILI9341_WriteCmd(ILI9341_PGAMCTRL);
    const uint8_t pgamma[] = {
        0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08,
        0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03,
        0x0E, 0x09, 0x00
    };
    ILI9341_WriteData(pgamma, 15);

    ILI9341_WriteCmd(ILI9341_NGAMCTRL);
    const uint8_t ngamma[] = {
        0x00, 0x0E, 0x14, 0x03, 0x11, 0x07,
        0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C,
        0x31, 0x36, 0x0F
    };
    ILI9341_WriteData(ngamma, 15);

    ILI9341_WriteCmd(ILI9341_DISPON); HAL_Delay(120);
}

void ILI9341_Init(void) {
    /* Повторяем инициализацию до 3 раз — защита от помех по питанию */
    for (int attempt = 0; attempt < 3; attempt++) {
        HAL_Delay(100);
        ILI9341_Reset();
        ILI9341_InitCommands();

        /* Проверка: заливаем небольшой прямоугольник и считаем успешным */
        ILI9341_FillRect(0, 0, 10, 10, 0xF800);  /* красный 10x10 */
        ILI9341_FillRect(0, 0, 10, 10, 0x0000);  /* гасим */

        /* Если дошли сюда без зависания — инициализация прошла */
        break;
    }

    ILI9341_FillScreen(0x0000);  /* Очистка */
}

/* =========================================================================
 * DMA движок (идентичен ST7789)
 * ========================================================================= */

#define LINEBUF_SIZE (ILI9341_WIDTH * 2)
static uint8_t linebuf[LINEBUF_SIZE];
static volatile bool dma_busy = false;
static struct { uint16_t w, h, cur; } job;

bool ILI9341_IsBusy(void) { return dma_busy; }

bool ILI9341_FillRectAsync(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (dma_busy) return false;
    job.w = w; job.h = h; job.cur = 0;

    uint8_t hi = color >> 8, lo = color & 0xFF;
    for (uint16_t i = 0; i < w * 2; i += 2) { linebuf[i] = hi; linebuf[i+1] = lo; }

    ILI9341_SetWindow(x, y, x+w-1, y+h-1);
    DC_Data();
    dma_busy = true;
    HAL_SPI_Transmit_DMA(&hspi1, linebuf, w * 2);
    return true;
}

bool ILI9341_BlitLineAsync(uint16_t x, uint16_t y, uint16_t w, const uint16_t *pixels) {
    if (dma_busy || w * 2 > LINEBUF_SIZE) return false;
    for (uint16_t i = 0; i < w; i++) {
        linebuf[i*2]   = pixels[i] >> 8;
        linebuf[i*2+1] = pixels[i] & 0xFF;
    }
    job.w = w; job.h = 1; job.cur = 0;
    ILI9341_SetWindow(x, y, x+w-1, y);
    DC_Data();
    dma_busy = true;
    HAL_SPI_Transmit_DMA(&hspi1, linebuf, w * 2);
    return true;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi != &hspi1 || !dma_busy) return;
    job.cur++;
    if (job.cur >= job.h) { dma_busy = false; return; }
    DC_Data();
    HAL_SPI_Transmit_DMA(&hspi1, linebuf, job.w * 2);
}

/* =========================================================================
 * Геометрия
 * ========================================================================= */

void ILI9341_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    while (dma_busy);
    ILI9341_SetWindow(x, y, x+w-1, y+h-1);
    DC_Data();
    uint8_t hi = color >> 8, lo = color & 0xFF;
    uint8_t buf[256];
    for (int i = 0; i < 256; i += 2) { buf[i] = hi; buf[i+1] = lo; }
    uint32_t total = (uint32_t)w * h * 2;
    while (total) {
        uint16_t n = (total > 256) ? 256 : (uint16_t)total;
        HAL_SPI_Transmit(&hspi1, buf, n, HAL_MAX_DELAY);
        total -= n;
    }
}

void ILI9341_FillScreen(uint16_t color) {
    ILI9341_FillRect(0, 0, ILI9341_WIDTH, ILI9341_HEIGHT, color);
}

void ILI9341_DrawPixel(uint16_t x, uint16_t y, uint16_t color) {
    if (dma_busy || x >= ILI9341_WIDTH || y >= ILI9341_HEIGHT) return;
    ILI9341_SetWindow(x, y, x, y);
    uint8_t d[2] = {color >> 8, color & 0xFF};
    DC_Data();
    HAL_SPI_Transmit(&hspi1, d, 2, HAL_MAX_DELAY);
}

void ILI9341_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    if (dma_busy) return;
    int16_t dx = abs(x1-x0), dy = abs(y1-y0);
    int16_t sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;
    while (1) {
        ILI9341_DrawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void ILI9341_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (dma_busy || w == 0 || h == 0) return;
    ILI9341_DrawLine(x,     y,     x+w-1, y,     color);
    ILI9341_DrawLine(x,     y,     x,     y+h-1, color);
    ILI9341_DrawLine(x+w-1, y,     x+w-1, y+h-1, color);
    ILI9341_DrawLine(x,     y+h-1, x+w-1, y+h-1, color);
}

void ILI9341_DrawCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color) {
    if (dma_busy || r == 0) return;
    int16_t f = 1-r, ddF_x = 1, ddF_y = -2*r, x = 0, y = r;
    ILI9341_DrawPixel(x0, y0+r, color); ILI9341_DrawPixel(x0, y0-r, color);
    ILI9341_DrawPixel(x0+r, y0, color); ILI9341_DrawPixel(x0-r, y0, color);
    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        ILI9341_DrawPixel(x0+x, y0+y, color); ILI9341_DrawPixel(x0-x, y0+y, color);
        ILI9341_DrawPixel(x0+x, y0-y, color); ILI9341_DrawPixel(x0-x, y0-y, color);
        ILI9341_DrawPixel(x0+y, y0+x, color); ILI9341_DrawPixel(x0-y, y0+x, color);
        ILI9341_DrawPixel(x0+y, y0-x, color); ILI9341_DrawPixel(x0-y, y0-x, color);
    }
}

void ILI9341_FillCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color) {
    if (dma_busy || r == 0) return;
    int16_t f = 1-r, ddF_x = 1, ddF_y = -2*r, x = 0, y = r;
    ILI9341_DrawLine(x0, y0-r, x0, y0+r, color);
    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        ILI9341_DrawLine(x0+x, y0-y, x0+x, y0+y, color);
        ILI9341_DrawLine(x0-x, y0-y, x0-x, y0+y, color);
        ILI9341_DrawLine(x0+y, y0-x, x0+y, y0+x, color);
        ILI9341_DrawLine(x0-y, y0-x, x0-y, y0+x, color);
    }
}

/* =========================================================================
 * Интерфейс для display.h
 * ========================================================================= */

const Display_Driver_t ili9341_interface = {
    .line_buffer   = (uint16_t *)linebuf,
    .screen_width  = ILI9341_WIDTH,
    .screen_height = ILI9341_HEIGHT,
    .FillRect      = ILI9341_FillRect,
    .DrawLine      = (void (*)(uint16_t, uint16_t, uint16_t, const uint16_t *))ILI9341_BlitLineAsync,
    .IsBusy        = ILI9341_IsBusy,
    .DrawCircle    = ILI9341_DrawCircle,
    .FillCircle    = ILI9341_FillCircle,
    .DrawRect      = ILI9341_DrawRect,
    .DrawVectorLine = ILI9341_DrawLine,
};
