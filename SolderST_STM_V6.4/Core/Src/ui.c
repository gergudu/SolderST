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

/* Высота заголовка (разделитель на y=30, текст выше) */
#define UI_HEADER_H        34

/* Шрифт меню — высота глифа определяет строку.
   AntiquaB_24: height≈28px, добавляем 7px зазора = 35px.
   Если сменить шрифт — поменять только здесь. */
#define UI_FONT_H          28
#define UI_LINE_GAP        7
#define UI_LINE_HEIGHT     (UI_FONT_H + UI_LINE_GAP)
#define UI_MENU_START_Y    (UI_HEADER_H + 2)

static char buf[32];
static SystemMode_t g_prevMode = SYS_MODE_MAIN_SOLDER;
static bool g_forceFullRedraw = false;

static uint8_t g_lastUICursor = 255;

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

    DISPLAY_FillRect(0, 30, sw, 1, GRAY);

    uint16_t tw = DISPLAY_GetTextWidth(title, &AntiquaB_24_uni);
    uint16_t x_pos = (sw > tw) ? (sw - tw) / 2 : 0;

    DISPLAY_Print(x_pos, 4, title, &AntiquaB_24_uni, titleColor, BLACK);
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


void UI_DrawMainScreen(bool is_solder) {
    // Текущая температура (Слот 11)
   /*выводв в целых*/
	int16_t cur = is_solder ? g_tCurrentSolder : g_tCurrentDesolder;
    snprintf(buf, sizeof(buf), "%3u", cur);
    DISPLAY_SmartPrint(11, 45, 75, buf, WHITE, BLACK, &Comic_40_dig);


    // Уставка (Слот 12)
    uint16_t set = is_solder ? g_TempSettings.targetSetSolder : g_TempSettings.targetSetDesolder;
    snprintf(buf, sizeof(buf), "Уст %3u", set);
    DISPLAY_SmartPrint(12, 10, 175, buf, GREEN, BLACK, &AntiquaB_24_uni);

    // Пресеты (Слоты 13, 14, 15)
    uint16_t* p = is_solder ? &g_TempSettings.preSet1Solder : &g_TempSettings.preSet1Desolder;

    for(int j = 0; j < 3; j++) {
        snprintf(buf, sizeof(buf), "%u", *(p + j));
        DISPLAY_SmartPrint(13 + j, 10 + (j * 95), 210, buf, WHITE, BLACK, &AntiquaB_24_uni);
    }
}

void UI_UpdateLoop(void) {
    SystemMode_t mode = STATE_GetMode();

    /* Полная перерисовка при смене режима */
    if (mode != g_prevMode || STATE_CheckAndResetDirty()) {
        DISPLAY_FillScreen(BLACK);
        DISPLAY_ClearAllSlots();

        g_lastUICursor = 255;
        g_forceFullRedraw = true;

        /* Заголовок только для не-warn режимов */
        if (mode != SYS_MODE_EXPERT_WARN) {
            UI_DrawHeader();
        }

        g_prevMode = mode;
    }

    if (mode == SYS_MODE_EXPERT_WARN) {
        UI_DrawExpertWarn();
    } else if (mode == SYS_MODE_EXPERT) {
        UI_DrawExpertMenu();
    } else if (mode == SYS_MODE_SERVICE) {
        UI_DrawServiceMenu();
    } else {
        UI_DrawMainScreen(mode == SYS_MODE_MAIN_SOLDER);
    }
}
