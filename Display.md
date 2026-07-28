Display Module Documentation
Общая информация

Модуль display.c/display.h предоставляет абстракцию для работы с графическими дисплеями на основе контроллеров типа ST7789, используя интерфейс Display_Driver_t. Все графические функции централизованы через этот модуль, что позволяет легко менять драйвер дисплея без изменения основной логики приложения.

Основные возможности:
Работа с графическими примитивами:
DISPLAY_FillRect – заливка прямоугольника
DISPLAY_DrawLine – рисование линии (включая горизонтальные/вертикальные линии)
DISPLAY_DrawRect – рисование рамки
DISPLAY_DrawCircle / DISPLAY_FillCircle – окружности и залитые круги

Работа с текстом:
DISPLAY_Print – вывод UTF-8 текста с поддержкой кастомных шрифтов
DISPLAY_SmartPrint – вывод текста с кэшированием для минимизации перерисовки
DISPLAY_GetTextWidth – вычисление ширины строки для текущего шрифта

Поддержка асинхронной передачи данных через DMA (для драйвера ST7789)

Базовые цветовые константы RGB565:
BLACK, WHITE, GRAY, DARK_GRAY, GREEN, CYAN, YELLOW, RED

Интерфейс драйвера Display_Driver_t:
typedef struct {
    uint16_t *line_buffer;
    uint16_t screen_width;
    uint16_t screen_height;


    void (*FillRect)(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    void (*DrawLine)(uint16_t x, uint16_t y, uint16_t len, const uint16_t *pixels);
    bool (*IsBusy)(void);


    void (*DrawCircle)(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);
    void (*FillCircle)(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);
    void (*DrawRect)(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    void (*DrawVectorLine)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
} Display_Driver_t;
Все функции графики и текста обращаются к зарегистрированному драйверу через этот интерфейс.

Инициализация и использование:
Подключить драйвер дисплея (например, st7789.c/h) и объявить глобальный объект: extern const Display_Driver_t st7789_interface;

Зарегистрировать драйвер в системе: DISPLAY_RegisterDriver(&st7789_interface);

Использовать графические и текстовые функции:
DISPLAY_FillScreen(BLACK);
DISPLAY_Print(10, 20, "Hello", &myFont, WHITE, BLACK);
DISPLAY_DrawCircle(60, 60, 20, RED);

Все функции автоматически проверяют готовность дисплея (IsBusy) перед началом отрисовки.

Инструкция для смены дисплея
Чтобы заменить ST7789 на другой контроллер (например, ST7735), необходимо:
1. Подключить новый драйвер
Создать/добавить st7735.c/h, реализующий функции интерфейса Display_Driver_t.
Обеспечить правильные макросы ширины и высоты:
#define ST7735_WIDTH  128
#define ST7735_HEIGHT 160

2. Проверить аппаратные сигналы
Пины SPI: SCK, MOSI, MISO (если используется)
Пины управления: DC, RST, CS (если CS не заземлен)
Установить правильные значения CPOL/CPHA в соответствии с документацией дисплея.

3. Настроить ориентацию и цветовую глубину
Установить MADCTL и COLMOD:
ST7735_WriteCommand(MADCTL);
ST7735_WriteDataByte(0x00); // ориентация
ST7735_WriteCommand(COLMOD);
ST7735_WriteDataByte(0x05); // 16-bit color RGB565

Проверить X/Y offset, если дисплей имеет смещение.

4. Зарегистрировать новый драйвер
extern const Display_Driver_t st7735_interface;
DISPLAY_RegisterDriver(&st7735_interface);
5. Проверить буфер scanline

В display.c заменить размер буфера на ширину нового дисплея:

static uint16_t scanline[ST7735_WIDTH];

Для универсальности можно использовать pDisp->screen_width вместо макроса.
После этого весь графический и текстовый код остаётся без изменений.
Рекомендации по стабильности и переносимости
Использовать функции DISPLAY_IsReady() перед каждой отрисовкой.
Проверять размеры дисплея при работе с координатами, особенно после смены контроллера.
Для больших заливок/картинок использовать DMA и асинхронные функции драйвера.
Для поддержки UTF-8 шрифтов убедиться, что font_t правильно заполнен LUT, смещения и ширины глифов.
При смене дисплея соблюдать одинаковый интерфейс Display_Driver_t, чтобы не менять display.c.
