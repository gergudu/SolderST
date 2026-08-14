/**
 * @file ui.c
 * @brief Отрисовка интерфейса через модуль DISPLAY
 */

#include "ui.h"
#include "state.h"
#include "config.h"
#include "fonts.h"
#include "display.h"
#include "main.h"
#include "heater.h"
#include "eeprom_i2c.h"
#include <stdio.h>
#include <string.h>

/* Внешние переменные температур и состояния */
extern uint16_t g_tCurrentSolder;
extern uint16_t g_tCurrentDesolder;

/* Настройки интерфейса — вычисляются из размеров дисплея при первом вызове */
#define UI_MARGIN_LEFT     10

/* Инфозона — верхняя полоса дисплея под системные индикаторы */
#define UI_INFO_ZONE_H     30

/* Индикатор записи EEPROM — кружок в инфозоне */
#define UI_INFO_EEPROM_X   16
#define UI_INFO_EEPROM_Y   15
#define UI_INFO_EEPROM_R   6

/* Таймеры сна в инфозоне */
#define UI_SLEEP_ICON_R    6
#define UI_SLEEP_BLOCK_W   70

/* Заголовки окон */
#define UI_HEADER_FONT     AntiquaB_16_uni
#define UI_HEADER_TEXT_Y   (UI_INFO_ZONE_H + 3)
#define UI_HEADER_ROW_H    22

/* Шрифт меню */
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

/* Последняя нарисованная строка блока таймера сна */
static char g_sleepBufSolder[12]   = "";
static char g_sleepBufDesolder[12] = "";

/* Статические переменные меню (вынесены для сброса при смене режима) */
static uint8_t svc_top          = 0;
static uint8_t svc_prev         = 255;
static bool    svc_rendered     = false;
static bool    svc_was_editing  = false;
static char    svc_prev_val[16] = {0};

static uint8_t expert_top          = 0;
static uint8_t last_expert_cursor = 255;
static bool    exp_rendered        = false;
static bool    exp_was_editing     = false;
static char    exp_prev_val[16]    = {0};

/* ========================================================================== */
/* Вспомогательные функции                                                    */
/* ========================================================================== */

static void FormatMenuValue(char* out_buf, size_t size, uint8_t idx, bool isSelected, bool isEditing) {
    const char* label = STATE_GetItemLabel(idx);

    if (strcmp(label, "Expert") == 0) {
        snprintf(out_buf, size, STATE_IsExpertWarn() ? "!! EXPERT !!" : "Expert");
        return;
    }

    uint16_t val = STATE_GetItemValue(idx);
    const char* fmt = (isEditing && isSelected) ? "[%3u]" : " %3u ";
    snprintf(out_buf, size, fmt, val);
}

static void DrawClockIcon(uint16_t cx, uint16_t cy, uint16_t r, uint16_t color) {
    DISPLAY_DrawCircle(cx, cy, r, color);
    DISPLAY_DrawLine(cx, cy, cx, cy - r + 2, color);
    DISPLAY_DrawLine(cx, cy, cx + (r * 2) / 3, cy - r / 3, color);
}

static void UI_DrawSleepTimerBlock(uint16_t rightEdgeX, uint16_t y, bool active,
                                    uint16_t counterTicks, bool inPresleep,
                                    const char *label, char *prevBuf) {
    char buf[12];

    if (!active) {
        if (prevBuf[0] != '\0') {
            DISPLAY_FillRect(rightEdgeX - UI_SLEEP_BLOCK_W, y - 2,
                             UI_SLEEP_BLOCK_W, UI_HEADER_FONT.height + 4, BLACK);
            prevBuf[0] = '\0';
        }
        return;
    }

    uint16_t totalSec = counterTicks * 30;
    snprintf(buf, 12, "%s %u:%02u", label, totalSec / 60, totalSec % 60);
    if (strcmp(buf, prevBuf) == 0) return;
    strncpy(prevBuf, buf, 11);
    prevBuf[11] = '\0';

    uint16_t color = inPresleep ? YELLOW : CYAN;
    uint16_t w     = DISPLAY_GetTextWidth(buf, &UI_HEADER_FONT);
    uint16_t cy    = y + UI_HEADER_FONT.height / 2;

    DISPLAY_FillRect(rightEdgeX - UI_SLEEP_BLOCK_W, y - 2,
                     UI_SLEEP_BLOCK_W, UI_HEADER_FONT.height + 4, BLACK);
    DrawClockIcon(rightEdgeX - w - 4 - UI_SLEEP_ICON_R, cy, UI_SLEEP_ICON_R, color);
    DISPLAY_Print(rightEdgeX - w, y, buf, &UI_HEADER_FONT, color, BLACK);
}

static void UI_DrawInfoZone(bool full_redraw) {
    uint16_t sw = DISPLAY_GetWidth();

    if (full_redraw) {
        DISPLAY_FillRect(0, 0, sw, UI_INFO_ZONE_H, BLACK);
        DISPLAY_FillRect(0, UI_INFO_ZONE_H - 1, sw, 1, GRAY);
        g_eepromDotPrev = -1;
        DISPLAY_ClearSlot(90);
        g_sleepBufSolder[0]   = '\xFF';
        g_sleepBufDesolder[0] = '\xFF';
    }

    bool dot_on = ((int32_t)(g_EepromFlashUntil - HAL_GetTick()) > 0);
    if ((int8_t)dot_on != g_eepromDotPrev) {
        DISPLAY_FillCircle(UI_INFO_EEPROM_X, UI_INFO_EEPROM_Y, UI_INFO_EEPROM_R,
                            dot_on ? WHITE : BLACK);
        g_eepromDotPrev = dot_on;
    }

    if (g_EepromFault) {
        uint16_t y = UI_INFO_ZONE_H > UI_HEADER_FONT.height
                   ? (UI_INFO_ZONE_H - UI_HEADER_FONT.height) / 2 : 0;
        DISPLAY_SmartPrint(90, UI_INFO_EEPROM_X + UI_INFO_EEPROM_R + 6, y,
                           "Err EEPROM", RED, BLACK, &UI_HEADER_FONT);
    }

    {
        uint16_t y    = UI_INFO_ZONE_H > UI_HEADER_FONT.height
                       ? (UI_INFO_ZONE_H - UI_HEADER_FONT.height) / 2 : 0;
        uint16_t half = sw / 2;

        UI_DrawSleepTimerBlock(half - 8, y,
                               CONFIG_IsSleepCounterActiveSolder(),
                               g_SleepCounters.counterSolder,
                               HEATER_GetStatusSolder().in_presleep,
                               "П", g_sleepBufSolder);

        UI_DrawSleepTimerBlock(sw - 8, y,
                               CONFIG_IsSleepCounterActiveDesolder(),
                               g_SleepCounters.counterDesolder,
                               HEATER_GetStatusDesolder().in_presleep,
                               "О", g_sleepBufDesolder);
    }
}

/* ========================================================================== */
/* Основная логика отрисовки                                                  */
/* ========================================================================== */

void UI_DrawExpertWarn(void) {
    uint16_t sw = DISPLAY_GetWidth();
    uint16_t sh = DISPLAY_GetHeight();

    DISPLAY_DrawRect(4, 35, sw - 8, sh - 40, YELLOW);

    const char *hdr = "!! ВНИМАНИЕ !!";
    uint16_t hw = DISPLAY_GetTextWidth(hdr, &AntiquaB_24_uni);
    DISPLAY_Print((sw - hw) / 2, 42, hdr, &AntiquaB_24_uni, RED, BLACK);

    DISPLAY_Print(10, 80,  "Режим требует", &AntiquaB_18_uni, YELLOW, BLACK);
    DISPLAY_Print(10, 105, "квалификации!", &AntiquaB_18_uni, YELLOW, BLACK);
    DISPLAY_Print(10, 135, "Неверные настройки", &AntiquaB_18_uni, WHITE, BLACK);
    DISPLAY_Print(10, 158, "могут повредить", &AntiquaB_18_uni, WHITE, BLACK);
    DISPLAY_Print(10, 181, "инструмент.", &AntiquaB_18_uni, WHITE, BLACK);

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

    uint8_t prevTop = expert_top;
    if (cursor < expert_top) expert_top = cursor;
    if (cursor >= expert_top + visible) expert_top = cursor - visible + 1;
    bool scrolled = (expert_top != prevTop);

    bool full_redraw  = g_forceFullRedraw || scrolled;
    bool cursor_moved = (cursor != last_expert_cursor);
    bool edit_changed = (isEditing != exp_was_editing);

    char cur_val[16] = {0};
    if (isEditing) snprintf(cur_val, sizeof(cur_val), "[%4u]", STATE_GetExpertItemValue(cursor));
    bool value_changed = (isEditing && strcmp(cur_val, exp_prev_val) != 0);

    if (!full_redraw && !cursor_moved && !edit_changed && !value_changed && exp_rendered) return;

    if (scrolled) {
        DISPLAY_FillRect(0, UI_MENU_START_Y, sw, sh - UI_MENU_START_Y, BLACK);
    }

    if (full_redraw) {
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

    uint8_t prevTop = svc_top;
    if (cursor < svc_top) svc_top = cursor;
    if (cursor >= svc_top + visible) svc_top = cursor - visible + 1;
    bool scrolled = (svc_top != prevTop);

    bool full_redraw  = (g_forceFullRedraw || scrolled);
    bool cursor_moved = (cursor != svc_prev);
    bool edit_changed = (isEditing != svc_was_editing);

    char cur_val[16] = {0};
    if (isEditing) FormatMenuValue(cur_val, sizeof(cur_val), cursor, true, true);
    bool value_changed = (isEditing && strcmp(cur_val, svc_prev_val) != 0);

    if (!full_redraw && !cursor_moved && !edit_changed && !value_changed && svc_rendered) return;

    if (scrolled) {
        DISPLAY_FillRect(0, UI_MENU_START_Y, sw, sh - UI_MENU_START_Y, BLACK);
    }

    if (full_redraw) {
        DISPLAY_ClearAllSlots();
        svc_prev     = 255;
        svc_rendered = false;
    }

    for (uint8_t vi = 0; vi < visible; vi++) {
        uint8_t i = svc_top + vi;
        if (i >= total) break;

        bool isSelected = (i == cursor);
        bool wasPrev    = (i == svc_prev);

        if (!full_redraw && !isSelected && !wasPrev) continue;

        uint16_t y        = UI_MENU_START_Y + vi * UI_LINE_HEIGHT;
        uint16_t bgColor  = isSelected ? DARK_GRAY : BLACK;
        uint16_t txtColor = isSelected ? (isEditing ? GREEN : CYAN) : WHITE;

        DISPLAY_FillRect(0, y - 2, sw, UI_LINE_HEIGHT, bgColor);
        DISPLAY_ClearSlot(1 + i);
        DISPLAY_ClearSlot(20 + i);

        DISPLAY_SmartPrint(1 + i, UI_MARGIN_LEFT, y,
                           STATE_GetItemLabel(i), txtColor, bgColor, &AntiquaB_24_uni);

        if (STATE_IsServiceItemAction(i)) continue;

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

static void UI_DrawToolColumn(uint16_t x0, uint16_t w, bool is_solder,
                               bool active_tool_is_solder,
                               RtdFault_t fault, bool enabled, bool force_clear) {
    uint16_t sh = DISPLAY_GetHeight();
    bool active = (is_solder == active_tool_is_solder);
    bool ok     = (fault == RTD_OK);
    bool faulty = !ok && (fault != RTD_NOT_CONNECTED);

    uint16_t labelColor   = !ok ? RED : (active ? CYAN : GRAY);
    uint16_t tempColor    = active ? WHITE : GRAY;
    uint16_t setColor     = active ? GREEN : GRAY;

    uint8_t slotLabel   = is_solder ? 10 : 60;
    uint8_t slotTemp    = is_solder ? 11 : 61;
    uint8_t slotSet     = is_solder ? 12 : 62;

    const char *label = is_solder ? "ПАЯЛЬНИК" : "ОТСОС";
    uint16_t lw = DISPLAY_GetTextWidth(label, &UI_HEADER_FONT);
    uint16_t lx = x0 + (w > lw ? (w - lw) / 2 : 0);
    DISPLAY_SmartPrint(slotLabel, lx, UI_HEADER_TEXT_Y, label, labelColor, BLACK, &UI_HEADER_FONT);

    const uint16_t topAvail    = UI_MENU_START_Y;
    const uint16_t bottomZoneH = 74;
    uint16_t bottomAvail = (sh > bottomZoneH) ? (sh - bottomZoneH) : topAvail;
    uint16_t availH = (bottomAvail > topAvail) ? (bottomAvail - topAvail) : 0;

    if (force_clear && !g_forceFullRedraw && availH > 0) {
        DISPLAY_FillRect(x0, topAvail, w, availH, BLACK);
    }

    if (!ok) {
        if (faulty) {
            const char *msg = "неисправно";
            uint16_t mw = DISPLAY_GetTextWidth(msg, &UI_HEADER_FONT);
            uint16_t mx = x0 + (w > mw ? (w - mw) / 2 : 0);
            uint16_t my = topAvail + ((availH > UI_HEADER_FONT.height) ? (availH - UI_HEADER_FONT.height) / 2 : 0);
            DISPLAY_SmartPrint(slotTemp, mx, my, msg, RED, BLACK, &UI_HEADER_FONT);
        } else {
            DISPLAY_ClearSlot(slotTemp);
        }
    } else if (!enabled) {
        const char *msg = "ВЫКЛ";
        uint16_t mw = DISPLAY_GetTextWidth(msg, &AntiquaB_24_uni);
        uint16_t mx = x0 + (w > mw ? (w - mw) / 2 : 0);
        uint16_t my = topAvail + ((availH > AntiquaB_24_uni.height) ? (availH - AntiquaB_24_uni.height) / 2 : 0);
        DISPLAY_SmartPrint(slotTemp, mx, my, msg, tempColor, BLACK, &AntiquaB_24_uni);
    } else {
        int16_t cur = is_solder ? g_tCurrentSolder : g_tCurrentDesolder;
        snprintf(buf, sizeof(buf), "%3u", (uint16_t)cur);
        uint16_t tw = DISPLAY_GetTextWidth(buf, &Comic_60_dig);
        uint16_t tx = x0 + (w > tw ? (w - tw) / 2 : 0);
        uint16_t ty = topAvail + ((availH > Comic_60_dig.height) ? (availH - Comic_60_dig.height) / 2 : 0);
        DISPLAY_SmartPrint(slotTemp, tx, ty, buf, tempColor, BLACK, &Comic_60_dig);
    }

    uint16_t set = is_solder ? g_TempSettings.targetSetSolder : g_TempSettings.targetSetDesolder;
    snprintf(buf, sizeof(buf), "Уст %3u", set);
    uint16_t sw2 = DISPLAY_GetTextWidth(buf, &AntiquaB_24_uni);
    uint16_t sx = x0 + (w > sw2 ? (w - sw2) / 2 : 0);
    uint16_t sy = (sh > 70) ? (sh - 70) : topAvail;
    DISPLAY_SmartPrint(slotSet, sx, sy, buf, setColor, BLACK, &AntiquaB_24_uni);
}

static void UI_DrawSharedPresetRow(bool active_is_solder) {
    uint16_t sw = DISPLAY_GetWidth();
    uint16_t sh = DISPLAY_GetHeight();

    uint16_t py   = (sh > 35) ? (sh - 35) : UI_MENU_START_Y;
    uint16_t colW = sw / 3;

    for (int j = 0; j < 3; j++) {
        uint16_t val = active_is_solder ? CONFIG_GetPresetSolder(j + 1)
                                         : CONFIG_GetPresetDesolder(j + 1);
        snprintf(buf, sizeof(buf), "%u", val);
        uint16_t pw = DISPLAY_GetTextWidth(buf, &AntiquaB_24_uni);
        uint16_t px = j * colW + (colW > pw ? (colW - pw) / 2 : 0);
        DISPLAY_SmartPrint(16 + j, px, py, buf, WHITE, BLACK, &AntiquaB_24_uni);
    }
}

void UI_DrawMainScreen(void) {
    static bool       tool_prev            = true;
    static RtdFault_t solderFaultPrev      = RTD_OK;
    static RtdFault_t desolderFaultPrev    = RTD_OK;
    static bool       solderEnabledPrev    = false;
    static bool       desolderEnabledPrev  = false;

    uint16_t sw   = DISPLAY_GetWidth();
    uint16_t sh   = DISPLAY_GetHeight();
    uint16_t half = sw / 2;

    bool tool_now = g_WorkFlags.tool;
    RtdFault_t solderFault   = HEATER_GetStatusSolder().rtd_fault;
    RtdFault_t desolderFault = HEATER_GetStatusDesolder().rtd_fault;
    bool solderEnabled   = g_WorkFlags.pwrIsOnSolder;
    bool desolderEnabled = g_WorkFlags.pwrIsOnVac;

    bool fault_changed   = (solderFault != solderFaultPrev) || (desolderFault != desolderFaultPrev);
    bool enabled_changed = (solderEnabled != solderEnabledPrev) || (desolderEnabled != desolderEnabledPrev);
    bool need_reset = tool_now != tool_prev || g_forceFullRedraw || fault_changed || enabled_changed;

    if (need_reset) {
        for (uint8_t s = 10; s <= 15; s++) DISPLAY_ClearSlot(s);
        for (uint8_t s = 60; s <= 65; s++) DISPLAY_ClearSlot(s);
        for (uint8_t s = 16; s <= 18; s++) DISPLAY_ClearSlot(s);
    }

    UI_DrawToolColumn(0,    half,      true,  tool_now, solderFault,   solderEnabled,   need_reset);
    UI_DrawToolColumn(half, sw - half, false, tool_now, desolderFault, desolderEnabled, need_reset);
    UI_DrawSharedPresetRow(tool_now);

    if (need_reset) {
        uint16_t presetY = (sh > 35) ? (sh - 35) : UI_MENU_START_Y;
        uint16_t vTop    = UI_INFO_ZONE_H;
        uint16_t vBottom = (presetY > vTop + 6) ? (presetY - 6) : vTop;
        DISPLAY_FillRect(half - 1, vTop, 2, vBottom - vTop, GRAY);
    }

    tool_prev           = tool_now;
    solderFaultPrev     = solderFault;
    desolderFaultPrev   = desolderFault;
    solderEnabledPrev   = solderEnabled;
    desolderEnabledPrev = desolderEnabled;
}

void UI_UpdateLoop(void) {
    SystemMode_t mode = STATE_GetMode();

    /* Считываем флаг из state.c строго один раз */
    bool needs_clear = STATE_CheckAndResetUINeedsClear();
    bool mode_changed = (mode != g_prevMode) || needs_clear;

    if (mode_changed) {
        DISPLAY_FillScreen(BLACK);
        DISPLAY_ClearAllSlots();

        g_lastUICursor = 255;
        g_forceFullRedraw = true;

        /* Сбрасываем внутренние состояния подсистем меню */
        svc_top = 0;
        svc_prev = 255;
        svc_rendered = false;
        svc_was_editing = false;
        svc_prev_val[0] = 0;

        expert_top = 0;
        last_expert_cursor = 255;
        exp_rendered = false;
        exp_was_editing = false;
        exp_prev_val[0] = 0;

        g_prevMode = mode;
    }

    UI_DrawInfoZone(mode_changed);

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

    /* Флаг сбрасывается в самом конце единого цикла */
    g_forceFullRedraw = false;
}
