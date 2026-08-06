/**
 * @file ui.c
 * @brief Отрисовка интерфейса через модуль DISPLAY
 */

#include "ui.h"
#include "state.h"
#include "config.h"
#include "fonts.h"
#include "display.h"
#include <stdio.h>
#include <string.h>

/* Внешние переменные температур и состояния */
extern uint16_t g_tCurrentSolder;
extern uint16_t g_tCurrentDesolder;
extern bool g_UI_NeedsClear;

/* Настройки интерфейса — вычисляются из размеров дисплея при первом вызове */
#define UI_MARGIN_LEFT     10

/* Инфозона — верхняя полоса дисплея под системные индикаторы
   (запись EEPROM, позже — таймеры сна и т.п.). Разделительная
   линия рисуется по её нижней границе (y = UI_INFO_ZONE_H - 1). */
#define UI_INFO_ZONE_H     30

/* Индикатор записи EEPROM — кружок в инфозоне */
#define UI_INFO_EEPROM_X   16
#define UI_INFO_EEPROM_Y   15
#define UI_INFO_EEPROM_R   6

/* Заголовки окон опущены под инфозону и используют шрифт 16
   (AntiquaB_16_uni: height=18px). */
#define UI_HEADER_FONT     AntiquaB_16_uni
#define UI_HEADER_TEXT_Y   (UI_INFO_ZONE_H + 3)
#define UI_HEADER_ROW_H    22   /* 18px шрифт + отступы сверху/снизу */

/* Шрифт меню — высота глифа определяет строку.
   AntiquaB_24: height≈28px, добавляем 7px зазора = 35px.
   Если сменить шрифт — поменять только здесь. */
#define UI_FONT_H          28
#define UI_LINE_GAP        7
#define UI_LINE_HEIGHT     (UI_FONT_H + UI_LINE_GAP)
#define UI_MENU_START_Y    (UI_INFO_ZONE_H + UI_HEADER_ROW_H + 2)

static char buf[32];
static SystemMode_t g_prevMode = SYS_MODE_MAIN_SOLDER;
static bool g_forceFullRedraw = false;

static uint8_t g_lastUICursor = 255;

/* Состояние индикатора записи EEPROM в инфозоне (-1 — принудительная перерисовка) */
static int8_t g_eepromDotPrev = -1;

/* ========================================================================== */
/* Вспомогательные функции (снижение цикломатической сложности)               */
/* ========================================================================== */

/**
 * @brief Подготовка строки значения для пункта меню
 */
static void FormatMenuValue(char* out_buf, size_t size, uint8_t idx, bool isSelected, bool isEditing) {
    const char* label = STATE_GetItemLabel(idx);

    // Специальная обработка для пункта Expert
    if (strcmp(label, "Expert") == 0) {
        snprintf(out_buf, size, STATE_IsExpertWarn() ? "!! EXPERT !!" : "Expert");
        return;
    }

    uint16_t val = STATE_GetItemValue(idx);

    // Пересчет специфических параметров (таймеры в минуты)
    if (strcmp(label, "PreSleep") == 0 || strcmp(label, "Standby") == 0) {
        val = (val * 30) / 60;
    }

    // Выбор формата: с рамками при редактировании или обычный
    const char* fmt = (isEditing && isSelected) ? "[%3u]" : " %3u ";
    snprintf(out_buf, size, fmt, val);
}

/**
 * @brief Отрисовка инфозоны (верхние 30px экрана).
 *
 * Рисуется на каждом проходе UI_UpdateLoop, независимо от текущего
 * режима — индикаторы должны быть видны всегда, поверх любого экрана.
 *
 * @param full_redraw  true — фон и рамка инфозоны были стёрты (смена
 *                      режима/полная перерисовка), нужно перерисовать
 *                      всё содержимое, а не только изменившееся.
 */
static void UI_DrawInfoZone(bool full_redraw) {
    uint16_t sw = DISPLAY_GetWidth();

    if (full_redraw) {
        DISPLAY_FillRect(0, 0, sw, UI_INFO_ZONE_H, BLACK);
        DISPLAY_FillRect(0, UI_INFO_ZONE_H - 1, sw, 1, GRAY);
        g_eepromDotPrev = -1; /* форсируем перерисовку индикатора ниже */
    }

    /* Индикатор записи EEPROM: горит только в момент реальной отложенной
       записи (после того как истёк дебаунс g_SaveDelayCounter и
       CONFIG_SaveToEEPROM() реально что-то записала), с удержанием на
       EEPROM_FLASH_TICKS — иначе сама запись длится доли миллисекунды
       и физически не видна на экране. Выставляется в main.c. */
    bool dot_on = (g_EepromFlashTicks > 0);
    if ((int8_t)dot_on != g_eepromDotPrev) {
        DISPLAY_FillCircle(UI_INFO_EEPROM_X, UI_INFO_EEPROM_Y, UI_INFO_EEPROM_R,
                            dot_on ? WHITE : BLACK);
        g_eepromDotPrev = dot_on;
    }

    /* TODO: индикаторы таймеров сна (PreSleep/Standby) — разместить
       правее, в свободной части инфозоны, отдельно для паяльника
       и отсоса. Резерв места уже заложен высотой инфозоны 30px. */
}

/* ========================================================================== */
/* Основная логика отрисовки                                                  */
/* ========================================================================== */

void UI_DrawExpertWarn(void) {
    uint16_t sw = DISPLAY_GetWidth();
    uint16_t sh = DISPLAY_GetHeight();

    /* Рамка */
    DISPLAY_DrawRect(4, 35, sw - 8, sh - 40, YELLOW);

    /* Заголовок */
    const char *hdr = "!! ВНИМАНИЕ !!";
    uint16_t hw = DISPLAY_GetTextWidth(hdr, &AntiquaB_24_uni);
    DISPLAY_Print((sw - hw) / 2, 42, hdr, &AntiquaB_24_uni, RED, BLACK);

    /* Текст предупреждения */
    DISPLAY_Print(10, 80,  "Режим требует", &AntiquaB_18_uni, YELLOW, BLACK);
    DISPLAY_Print(10, 105, "квалификации!", &AntiquaB_18_uni, YELLOW, BLACK);
    DISPLAY_Print(10, 135, "Неверные настройки", &AntiquaB_18_uni, WHITE, BLACK);
    DISPLAY_Print(10, 158, "могут повредить", &AntiquaB_18_uni, WHITE, BLACK);
    DISPLAY_Print(10, 181, "инструмент.", &AntiquaB_18_uni, WHITE, BLACK);

    /* Подсказка */
    const char *hint = "SET2 long = войти";
    uint16_t yw = DISPLAY_GetTextWidth(hint, &AntiquaB_18_uni);
    DISPLAY_Print((sw - yw) / 2, 210, hint, &AntiquaB_18_uni, CYAN, BLACK);
}

void UI_DrawExpertMenu(void) {
    uint8_t cursor   = STATE_GetExpertCursor();
    uint8_t total    = STATE_GetExpertTotalItems();
    bool    isEditing = STATE_IsExpertEditing();
    uint16_t sw      = DISPLAY_GetWidth();
    uint16_t sh      = DISPLAY_GetHeight();

    uint8_t visible = (uint8_t)((sh - UI_MENU_START_Y) / UI_LINE_HEIGHT);

    static uint8_t expert_top      = 0;
    static uint8_t last_expert_cursor = 255;
    static bool    exp_rendered    = false;
    static bool    exp_was_editing = false;
    static char    exp_prev_val[16] = {0};

    uint8_t prevTop = expert_top;
    if (cursor < expert_top) expert_top = cursor;
    if (cursor >= expert_top + visible) expert_top = cursor - visible + 1;
    bool scrolled = (expert_top != prevTop);

    bool full_redraw  = STATE_CheckAndResetDirty() || g_forceFullRedraw || scrolled;
    bool cursor_moved = (cursor != last_expert_cursor);
    bool edit_changed = (isEditing != exp_was_editing);

    char cur_val[16] = {0};
    if (isEditing) snprintf(cur_val, sizeof(cur_val), "[%4u]", STATE_GetExpertItemValue(cursor));
    bool value_changed = (isEditing && strcmp(cur_val, exp_prev_val) != 0);

    /* Ничего не изменилось — выходим */
    if (!full_redraw && !cursor_moved && !edit_changed && !value_changed && exp_rendered) return;

    if (full_redraw) {
        DISPLAY_FillRect(0, UI_MENU_START_Y, sw, sh - UI_MENU_START_Y, BLACK);
        for (uint8_t vi = 0; vi < visible; vi++) {
            uint8_t idx = expert_top + vi;
            if (idx >= total) break;
            DISPLAY_ClearSlot(30 + idx);
            DISPLAY_ClearSlot(50 + idx);
        }
        last_expert_cursor = 255;
    }

    for (uint8_t vi = 0; vi < visible; vi++) {
        uint8_t idx = expert_top + vi;
        if (idx >= total) break;

        bool isSelected = (idx == cursor);
        bool wasPrev    = (idx == last_expert_cursor);

        if (!full_redraw && !isSelected && !wasPrev) continue;

        uint16_t y = UI_MENU_START_Y + vi * UI_LINE_HEIGHT;
        uint16_t bgColor   = isSelected ? DARK_GRAY : BLACK;
        uint16_t textColor = isSelected ? (isEditing ? GREEN : CYAN) : WHITE;

        DISPLAY_FillRect(0, y - 2, sw, UI_LINE_HEIGHT, bgColor);
        DISPLAY_ClearSlot(30 + idx);
        DISPLAY_ClearSlot(50 + idx);

        const char *lbl = STATE_GetExpertItemLabel(idx);
        DISPLAY_SmartPrint(30 + idx, UI_MARGIN_LEFT, y, lbl, textColor, bgColor, &AntiquaB_24_uni);

        if (strcmp(lbl, "Выход") == 0 || strcmp(lbl, "Сброс") == 0) continue;

        snprintf(buf, sizeof(buf),
                 (isEditing && isSelected) ? "[%4u]" : " %4u ",
                 STATE_GetExpertItemValue(idx));
        uint16_t x_val = sw - 10 - DISPLAY_GetTextWidth(buf, &AntiquaB_24_uni);
        DISPLAY_SmartPrint(50 + idx, x_val, y, buf, textColor, bgColor, &AntiquaB_24_uni);
    }

    last_expert_cursor = cursor;
    g_forceFullRedraw  = false;
    exp_rendered       = true;
    exp_was_editing    = isEditing;
    if (isEditing) strncpy(exp_prev_val, cur_val, sizeof(exp_prev_val)-1);
    else exp_prev_val[0] = 0;
}

void UI_DrawHeader(void) {
    SystemMode_t mode = STATE_GetMode();
    char title[24];
    uint16_t titleColor = WHITE;
    uint16_t sw = DISPLAY_GetWidth();

    if (mode == SYS_MODE_SERVICE) {
        snprintf(title, sizeof(title), "SETUP %s", g_WorkFlags.tool ? "ПАЯЛЬНИК" : "ОТСОС");
        titleColor = YELLOW;
    } else if (mode == SYS_MODE_EXPERT) {
        snprintf(title, sizeof(title), "EXPERT %s", g_WorkFlags.tool ? "ПАЯЛЬНИК" : "ОТСОС");
        titleColor = RED;
    } else {
        snprintf(title, sizeof(title), "%s", (mode == SYS_MODE_MAIN_SOLDER) ? "ПАЯЛЬНИК" : "ОТСОС");
        titleColor = CYAN;
    }

    /* Разделительная линия под инфозоной рисуется в UI_DrawInfoZone() */

    uint16_t tw = DISPLAY_GetTextWidth(title, &UI_HEADER_FONT);
    uint16_t x_pos = (sw > tw) ? (sw - tw) / 2 : 0;

    DISPLAY_Print(x_pos, UI_HEADER_TEXT_Y, title, &UI_HEADER_FONT, titleColor, BLACK);
}

void UI_DrawServiceMenu(void) {
    uint8_t cursor    = STATE_GetMenuCursor();
    uint8_t total     = STATE_GetMenuTotalItems();
    bool    isEditing = STATE_IsEditing();
    uint16_t sw       = DISPLAY_GetWidth();
    uint16_t sh       = DISPLAY_GetHeight();

    uint8_t visible = (uint8_t)((sh - UI_MENU_START_Y) / UI_LINE_HEIGHT);

    static uint8_t svc_top      = 0;
    static uint8_t svc_prev     = 255;
    static bool    svc_rendered = false;
    static bool    svc_was_editing = false;
    static char    svc_prev_val[16] = {0};

    uint8_t prevTop = svc_top;
    if (cursor < svc_top) svc_top = cursor;
    if (cursor >= svc_top + visible) svc_top = cursor - visible + 1;
    bool scrolled = (svc_top != prevTop);

    bool full_redraw  = (g_UI_NeedsClear || g_forceFullRedraw || scrolled);
    bool cursor_moved = (cursor != svc_prev);
    bool edit_changed = (isEditing != svc_was_editing);

    char cur_val[16] = {0};
    if (isEditing) FormatMenuValue(cur_val, sizeof(cur_val), cursor, true, true);
    bool value_changed = (isEditing && strcmp(cur_val, svc_prev_val) != 0);

    if (!full_redraw && !cursor_moved && !edit_changed && !value_changed && svc_rendered) return;

    if (full_redraw) {
        DISPLAY_FillRect(0, UI_MENU_START_Y, sw, sh - UI_MENU_START_Y, BLACK);
        DISPLAY_ClearAllSlots();
        g_UI_NeedsClear   = false;
        g_forceFullRedraw = false;
        svc_prev          = 255;
        svc_rendered      = false;
    }

    for (uint8_t vi = 0; vi < visible; vi++) {
        uint8_t i = svc_top + vi;
        if (i >= total) break;

        bool isSelected = (i == cursor);
        bool wasPrev    = (i == svc_prev);

        /* Перерисовываем только изменившиеся строки */
        if (!full_redraw && !isSelected && !wasPrev) continue;

        uint16_t y        = UI_MENU_START_Y + vi * UI_LINE_HEIGHT;
        uint16_t bgColor  = isSelected ? DARK_GRAY : BLACK;
        uint16_t txtColor = isSelected ? (isEditing ? GREEN : CYAN) : WHITE;

        /* Фон + сброс слотов чтобы SmartPrint перерисовал текст */
        DISPLAY_FillRect(0, y - 2, sw, UI_LINE_HEIGHT, bgColor);
        DISPLAY_ClearSlot(1 + i);
        DISPLAY_ClearSlot(20 + i);

        /* Лейбл */
        DISPLAY_SmartPrint(1 + i, UI_MARGIN_LEFT, y,
                           STATE_GetItemLabel(i), txtColor, bgColor, &AntiquaB_24_uni);

        /* Значение */
        const char *lbl = STATE_GetItemLabel(i);
        if (strcmp(lbl, "Выход") == 0 || strcmp(lbl, "Expert") == 0) continue;

        FormatMenuValue(buf, sizeof(buf), i, isSelected, isEditing);
        uint16_t x_val = sw - 15 - DISPLAY_GetTextWidth(buf, &AntiquaB_24_uni);
        DISPLAY_SmartPrint(20 + i, x_val, y, buf, txtColor, bgColor, &AntiquaB_24_uni);
    }

    g_lastUICursor  = cursor;
    svc_prev        = cursor;
    svc_rendered    = true;
    svc_was_editing = isEditing;
    if (isEditing) strncpy(svc_prev_val, cur_val, sizeof(svc_prev_val)-1);
    else svc_prev_val[0] = 0;
}


/**
 * @brief Отрисовка одной колонки главного экрана (паяльник или отсос).
 * @param x0                Левая граница колонки в пикселях
 * @param w                 Ширина колонки в пикселях
 * @param is_solder         true — это колонка паяльника, false — отсоса
 * @param active_tool_is_solder  true, если сейчас выбран (активен) паяльник
 */
static void UI_DrawToolColumn(uint16_t x0, uint16_t w, bool is_solder, bool active_tool_is_solder) {
    uint16_t sh = DISPLAY_GetHeight();
    bool active = (is_solder == active_tool_is_solder);

    /* Неактивный инструмент — целиком серый (подпись и температура,
       остальное для единообразия туда же) */
    uint16_t labelColor   = active ? CYAN  : GRAY;
    uint16_t tempColor    = active ? WHITE : GRAY;
    uint16_t setColor     = active ? GREEN : GRAY;

    uint8_t slotLabel   = is_solder ? 10 : 60;
    uint8_t slotTemp    = is_solder ? 11 : 61;
    uint8_t slotSet     = is_solder ? 12 : 62;

    /* Подпись окна — под инфозоной, шрифт 16px */
    const char *label = is_solder ? "ПАЯЛЬНИК" : "ОТСОС";
    uint16_t lw = DISPLAY_GetTextWidth(label, &UI_HEADER_FONT);
    uint16_t lx = x0 + (w > lw ? (w - lw) / 2 : 0);
    DISPLAY_SmartPrint(slotLabel, lx, UI_HEADER_TEXT_Y, label, labelColor, BLACK, &UI_HEADER_FONT);

    /* Текущая температура — по вертикали центрируется в зоне между
       строкой заголовка и блоком уставки внизу */
    int16_t cur = is_solder ? g_tCurrentSolder : g_tCurrentDesolder;
    snprintf(buf, sizeof(buf), "%3u", (uint16_t)cur);
    uint16_t tw = DISPLAY_GetTextWidth(buf, &Comic_40_dig);
    uint16_t tx = x0 + (w > tw ? (w - tw) / 2 : 0);

    const uint16_t topAvail    = UI_MENU_START_Y;
    const uint16_t bottomZoneH = 74; /* резерв под "Уст" + общую строку пресетов */
    uint16_t bottomAvail = (sh > bottomZoneH) ? (sh - bottomZoneH) : topAvail;
    uint16_t availH = (bottomAvail > topAvail) ? (bottomAvail - topAvail) : Comic_40_dig.height;
    uint16_t ty = topAvail + ((availH > Comic_40_dig.height) ? (availH - Comic_40_dig.height) / 2 : 0);
    DISPLAY_SmartPrint(slotTemp, tx, ty, buf, tempColor, BLACK, &Comic_40_dig);

    /* Уставка — своя для каждого инструмента */
    uint16_t set = is_solder ? g_TempSettings.targetSetSolder : g_TempSettings.targetSetDesolder;
    snprintf(buf, sizeof(buf), "Уст %3u", set);
    uint16_t sw2 = DISPLAY_GetTextWidth(buf, &AntiquaB_24_uni);
    uint16_t sx = x0 + (w > sw2 ? (w - sw2) / 2 : 0);
    uint16_t sy = (sh > 70) ? (sh - 70) : topAvail;
    DISPLAY_SmartPrint(slotSet, sx, sy, buf, setColor, BLACK, &AntiquaB_24_uni);
}

/**
 * @brief Отрисовка общей строки пресетов SET1-3 (на всю ширину экрана,
 *        одна на оба инструмента — показывает пресеты активного инструмента).
 *        Не делится вертикальной полосой между окнами.
 * @param active_is_solder  true — показать пресеты паяльника, иначе отсоса
 */
static void UI_DrawSharedPresetRow(bool active_is_solder) {
    uint16_t sw = DISPLAY_GetWidth();
    uint16_t sh = DISPLAY_GetHeight();

    uint16_t *p = active_is_solder ? &g_TempSettings.preSet1Solder
                                    : &g_TempSettings.preSet1Desolder;
    uint16_t py   = (sh > 35) ? (sh - 35) : UI_MENU_START_Y;
    uint16_t colW = sw / 3;

    for (int j = 0; j < 3; j++) {
        snprintf(buf, sizeof(buf), "%u", *(p + j));
        uint16_t pw = DISPLAY_GetTextWidth(buf, &AntiquaB_24_uni);
        uint16_t px = j * colW + (colW > pw ? (colW - pw) / 2 : 0);
        DISPLAY_SmartPrint(16 + j, px, py, buf, WHITE, BLACK, &AntiquaB_24_uni);
    }
}

void UI_DrawMainScreen(void) {
    static bool tool_prev = true;

    uint16_t sw   = DISPLAY_GetWidth();
    uint16_t sh   = DISPLAY_GetHeight();
    uint16_t half = sw / 2;

    bool tool_now = g_WorkFlags.tool; /* true = активен паяльник */

    /* Разделительные линии перерисовываем только при входе на экран
       (полная перерисовка), не каждый кадр. Горизонтальная линия под
       инфозоной уже рисуется в UI_DrawInfoZone(). Вертикальная линия
       между окнами не должна заходить в инфозону (сверху) и в общую
       строку пресетов SET1-3 (снизу) — она их не разделяет. */
    if (g_forceFullRedraw) {
        uint16_t presetY = (sh > 35) ? (sh - 35) : UI_MENU_START_Y;
        uint16_t vTop    = UI_INFO_ZONE_H;
        uint16_t vBottom = (presetY > vTop + 6) ? (presetY - 6) : vTop;
        DISPLAY_FillRect(half - 1, vTop, 2, vBottom - vTop, GRAY);
    }

    /* Если сменился активный инструмент — обе колонки перекрашиваются
       (активная/неактивная меняются местами), плюс общая строка
       пресетов показывает значения другого инструмента. SmartPrint
       сравнивает только текст, а не цвет, поэтому без сброса слотов
       перекраска была бы пропущена, если сами цифры не изменились. */
    if (tool_now != tool_prev || g_forceFullRedraw) {
        for (uint8_t s = 10; s <= 15; s++) DISPLAY_ClearSlot(s);
        for (uint8_t s = 60; s <= 65; s++) DISPLAY_ClearSlot(s);
        for (uint8_t s = 16; s <= 18; s++) DISPLAY_ClearSlot(s);
    }

    UI_DrawToolColumn(0,    half,      true,  tool_now);
    UI_DrawToolColumn(half, sw - half, false, tool_now);
    UI_DrawSharedPresetRow(tool_now);

    tool_prev = tool_now;
    g_forceFullRedraw = false;
}

void UI_UpdateLoop(void) {
    SystemMode_t mode = STATE_GetMode();

    /* Полная перерисовка при смене режима */
    bool mode_changed = (mode != g_prevMode) || STATE_CheckAndResetDirty();
    if (mode_changed) {
        DISPLAY_FillScreen(BLACK);
        DISPLAY_ClearAllSlots();

        g_lastUICursor = 255;
        g_forceFullRedraw = true;

        g_prevMode = mode;
    }

    /* Инфозона (индикаторы) рисуется на каждом проходе, поверх
       любого режима — включая экран EXPERT_WARN. */
    UI_DrawInfoZone(mode_changed);

    /* Заголовок — только для не-warn и не-главных режимов:
       у главного экрана теперь своя подпись в каждом окне */
    if (mode_changed && mode != SYS_MODE_EXPERT_WARN &&
        mode != SYS_MODE_MAIN_SOLDER && mode != SYS_MODE_MAIN_DESOLDER) {
        UI_DrawHeader();
    }

    if (mode == SYS_MODE_EXPERT_WARN) {
        UI_DrawExpertWarn();
    } else if (mode == SYS_MODE_EXPERT) {
        UI_DrawExpertMenu();
    } else if (mode == SYS_MODE_SERVICE) {
        UI_DrawServiceMenu();
    } else {
        UI_DrawMainScreen();
    }
}
