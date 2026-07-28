#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "fonts.h"

#define BLACK       0x0000
#define WHITE       0xFFFF
#define GRAY        0x8410
#define DARK_GRAY   0x4208
#define GREEN       0x07E0
#define CYAN        0x07FF
#define YELLOW      0xFFE0
#define RED         0xF800

typedef struct {
    uint16_t *line_buffer;
    uint16_t screen_width;
    uint16_t screen_height;

    void (*FillRect)(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t color);
    void (*DrawLine)(uint16_t x,uint16_t y,uint16_t len,const uint16_t *pixels);
    bool (*IsBusy)(void);

    void (*DrawCircle)(uint16_t x0,uint16_t y0,uint16_t r,uint16_t color);
    void (*FillCircle)(uint16_t x0,uint16_t y0,uint16_t r,uint16_t color);
    void (*DrawRect)(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t color);
    void (*DrawVectorLine)(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1,uint16_t color);
} Display_Driver_t;

void DISPLAY_RegisterDriver(const Display_Driver_t *driver);
uint16_t DISPLAY_GetWidth(void);
uint16_t DISPLAY_GetHeight(void);
bool DISPLAY_IsReady(void);

void DISPLAY_FillRect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t color);
void DISPLAY_FillScreen(uint16_t color);
void DISPLAY_DrawCircle(uint16_t x,uint16_t y,uint16_t r,uint16_t color);
void DISPLAY_FillCircle(uint16_t x,uint16_t y,uint16_t r,uint16_t color);
void DISPLAY_DrawRect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t color);
void DISPLAY_DrawLine(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1,uint16_t color);

void DISPLAY_Print(uint16_t x,uint16_t y,const char* str,const font_t* font,uint16_t color,uint16_t bgcolor);
void DISPLAY_SmartPrint(uint8_t slot,uint16_t x,uint16_t y,const char* str,uint16_t color,uint16_t bgcolor,const font_t* font);
uint16_t DISPLAY_GetTextWidth(const char* str,const font_t* font);
void DISPLAY_ClearAllSlots(void);
void DISPLAY_ClearSlot(uint8_t slot);

#endif /* DISPLAY_H */
