#include "st7789.h"
#include "main.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdlib.h>

extern SPI_HandleTypeDef hspi1;

#define DC_PORT   Disp_DC_GPIO_Port
#define DC_PIN    Disp_DC_Pin
#define RST_PORT  Disp_RST_GPIO_Port
#define RST_PIN   Disp_RST_Pin

static inline void DC_Command(void) { HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_RESET); }
static inline void DC_Data(void)    { HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_SET); }

static void ST7789_WriteCommand(uint8_t cmd) { DC_Command(); HAL_SPI_Transmit(&hspi1, &cmd, 1, HAL_MAX_DELAY); }
static void ST7789_WriteDataByte(uint8_t data){ DC_Data(); HAL_SPI_Transmit(&hspi1, &data, 1, HAL_MAX_DELAY); }
void ST7789_WriteData(uint8_t *data, uint16_t len) { DC_Data(); HAL_SPI_Transmit(&hspi1, data, len, HAL_MAX_DELAY); }

static void ST7789_Reset(void){
    HAL_GPIO_WritePin(RST_PORT, RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(RST_PORT, RST_PIN, GPIO_PIN_SET);
    HAL_Delay(150);
}

void ST7789_SetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1){
    uint8_t d[4];
    ST7789_WriteCommand(CASET);
    d[0] = x0 >> 8; d[1] = x0 & 0xFF;
    d[2] = x1 >> 8; d[3] = x1 & 0xFF;
    ST7789_WriteData(d, 4);

    ST7789_WriteCommand(RASET);
    y0 += Y_OFFSET; y1 += Y_OFFSET;
    d[0] = y0 >> 8; d[1] = y0 & 0xFF;
    d[2] = y1 >> 8; d[3] = y1 & 0xFF;
    ST7789_WriteData(d, 4);
}

/* ===== Инициализация ===== */
void ST7789_Init(void){
    HAL_Delay(100);
    ST7789_Reset();

    ST7789_WriteCommand(SWRESET); HAL_Delay(150);
    ST7789_WriteCommand(SLPOUT); HAL_Delay(120);
    ST7789_WriteCommand(COLMOD); ST7789_WriteDataByte(0x55); // RGB565

    ST7789_WriteCommand(0xB7); ST7789_WriteDataByte(0x44);
    ST7789_WriteCommand(CMD_VCOMS); ST7789_WriteDataByte(0x24);
    ST7789_WriteCommand(CMD_VRHS); ST7789_WriteDataByte(0x13);
    ST7789_WriteCommand(CMD_PWCTRL1);
    uint8_t pw_data[] = {0xA4, 0xA1}; ST7789_WriteData(pw_data, 2);

    ST7789_WriteCommand(CMD_RAMCTRL);
    uint8_t ram_data[] = {0x00, 0xC0}; ST7789_WriteData(ram_data, 2);

    ST7789_WriteCommand(MADCTL); ST7789_WriteDataByte(0x70); // Landscape 320x240

    uint8_t pv_gamma[14] = {0xD0,0x00,0x02,0x07,0x0A,0x28,0x32,0x44,0x42,0x06,0x0E,0x12,0x14,0x17};
    ST7789_WriteCommand(CMD_PVGAMCTRL); ST7789_WriteData(pv_gamma,14);

    uint8_t nv_gamma[14] = {0xD0,0x00,0x02,0x07,0x0A,0x28,0x31,0x54,0x47,0x0E,0x1C,0x17,0x1B,0x1E};
    ST7789_WriteCommand(CMD_NVGAMCTRL); ST7789_WriteData(nv_gamma,14);

    ST7789_WriteCommand(INVON);
    ST7789_SetAddressWindow(0,0,ST7789_WIDTH-1,ST7789_HEIGHT-1);
    ST7789_WriteCommand(DISPON); HAL_Delay(120);
    ST7789_FillScreen(BLACK);
}

/* ===== DMA движок ===== */
#define LINEBUF_SIZE (ST7789_WIDTH*2)
static uint8_t linebuf[LINEBUF_SIZE];
static volatile bool dma_busy = false;
static struct { uint16_t w,h,cur; } job;

bool ST7789_IsBusy(void){ return dma_busy; }

bool ST7789_FillRectAsync(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t color){
    if(dma_busy) return false;
    job.w=w; job.h=h; job.cur=0;

    uint8_t hi=color>>8, lo=color&0xFF;
    for(uint16_t i=0;i<w*2;i+=2){ linebuf[i]=hi; linebuf[i+1]=lo; }

    ST7789_SetAddressWindow(x,y,x+w-1,y+h-1);
    ST7789_WriteCommand(RAMWR);
    DC_Data();
    dma_busy=true;
    HAL_SPI_Transmit_DMA(&hspi1,linebuf,w*2);
    return true;
}

bool ST7789_BlitLineAsync(uint16_t x,uint16_t y,uint16_t w,const uint16_t *pixels){
    if(dma_busy || w*2>LINEBUF_SIZE) return false;
    for(uint16_t i=0;i<w;i++){
        linebuf[i*2] = pixels[i]>>8;
        linebuf[i*2+1] = pixels[i]&0xFF;
    }
    job.w=w; job.h=1; job.cur=0;
    ST7789_SetAddressWindow(x,y,x+w-1,y);
    ST7789_WriteCommand(RAMWR);
    DC_Data();
    dma_busy=true;
    HAL_SPI_Transmit_DMA(&hspi1,linebuf,w*2);
    return true;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi){
    if(hspi!=&hspi1 || !dma_busy) return;
    job.cur++;
    if(job.cur>=job.h){ dma_busy=false; return; }
    DC_Data(); HAL_SPI_Transmit_DMA(&hspi1,linebuf,job.w*2);
}

/* ===== Геометрия ===== */
void ST7789_FillRect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t color){
    while(dma_busy);
    ST7789_SetAddressWindow(x,y,x+w-1,y+h-1);
    ST7789_WriteCommand(RAMWR);
    DC_Data();
    uint8_t hi=color>>8, lo=color&0xFF;
    uint8_t buf[256];
    for(int i=0;i<256;i+=2){ buf[i]=hi; buf[i+1]=lo; }
    uint32_t total = (uint32_t)w*h*2;
    while(total){
        uint16_t n = (total>256)?256:(uint16_t)total;
        HAL_SPI_Transmit(&hspi1,buf,n,HAL_MAX_DELAY);
        total-=n;
    }
}

void ST7789_FillScreen(uint16_t color){ ST7789_FillRect(0,0,ST7789_WIDTH,ST7789_HEIGHT,color); }
void ST7789_DrawPixel(uint16_t x,uint16_t y,uint16_t color){
    if(dma_busy || x>=ST7789_WIDTH || y>=ST7789_HEIGHT) return;
    ST7789_SetAddressWindow(x,y,x,y);
    ST7789_WriteCommand(RAMWR);
    uint8_t d[2] = {color>>8, color&0xFF};
    ST7789_WriteData(d,2);
}

void ST7789_DrawLine(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1,uint16_t color){
    if(dma_busy) return;
    int16_t dx=abs(x1-x0), dy=abs(y1-y0);
    int16_t sx=(x0<x1)?1:-1, sy=(y0<y1)?1:-1;
    int16_t err=dx-dy;
    while(1){
        ST7789_DrawPixel(x0,y0,color);
        if(x0==x1 && y0==y1) break;
        int16_t e2=2*err;
        if(e2>-dy){ err-=dy; x0+=sx; }
        if(e2<dx){ err+=dx; y0+=sy; }
    }
}

void ST7789_DrawRect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t color){
    if(dma_busy || w==0 || h==0) return;
    ST7789_DrawLine(x,y,x+w-1,y,color);
    ST7789_DrawLine(x,y,x,y+h-1,color);
    ST7789_DrawLine(x+w-1,y,x+w-1,y+h-1,color);
    ST7789_DrawLine(x,y+h-1,x+w-1,y+h-1,color);
}

void ST7789_DrawCircle(uint16_t x0,uint16_t y0,uint16_t r,uint16_t color){
    if(dma_busy || r==0) return;
    int16_t f=1-r, ddF_x=1, ddF_y=-2*r, x=0, y=r;
    ST7789_DrawPixel(x0,y0+r,color); ST7789_DrawPixel(x0,y0-r,color);
    ST7789_DrawPixel(x0+r,y0,color); ST7789_DrawPixel(x0-r,y0,color);
    while(x<y){
        if(f>=0){ y--; ddF_y+=2; f+=ddF_y; }
        x++; ddF_x+=2; f+=ddF_x;
        ST7789_DrawPixel(x0+x,y0+y,color); ST7789_DrawPixel(x0-x,y0+y,color);
        ST7789_DrawPixel(x0+x,y0-y,color); ST7789_DrawPixel(x0-x,y0-y,color);
        ST7789_DrawPixel(x0+y,y0+x,color); ST7789_DrawPixel(x0-y,y0+x,color);
        ST7789_DrawPixel(x0+y,y0-x,color); ST7789_DrawPixel(x0-y,y0-x,color);
    }
}

void ST7789_FillCircle(uint16_t x0,uint16_t y0,uint16_t r,uint16_t color){
    if(dma_busy || r==0) return;
    int16_t f=1-r, ddF_x=1, ddF_y=-2*r, x=0, y=r;
    ST7789_DrawLine(x0,y0-r,x0,y0+r,color);
    while(x<y){
        if(f>=0){ y--; ddF_y+=2; f+=ddF_y; }
        x++; ddF_x+=2; f+=ddF_x;
        ST7789_DrawLine(x0+x,y0-y,x0+x,y0+y,color);
        ST7789_DrawLine(x0-x,y0-y,x0-x,y0+y,color);
        ST7789_DrawLine(x0+y,y0-x,x0+y,y0+x,color);
        ST7789_DrawLine(x0-y,y0-x,x0-y,y0+x,color);
    }
}

/* ===== Интерфейс для DISPLAY.H ===== */
const Display_Driver_t st7789_interface = {
    .line_buffer    = (uint16_t*)linebuf,
    .screen_width   = ST7789_WIDTH,
    .screen_height  = ST7789_HEIGHT,
    .FillRect       = ST7789_FillRect,
    .DrawLine       = (void (*)(uint16_t,uint16_t,uint16_t,const uint16_t*))ST7789_BlitLineAsync,
    .IsBusy         = ST7789_IsBusy,
    .DrawCircle     = ST7789_DrawCircle,
    .FillCircle     = ST7789_FillCircle,
    .DrawRect       = ST7789_DrawRect,
    .DrawVectorLine = ST7789_DrawLine
};
