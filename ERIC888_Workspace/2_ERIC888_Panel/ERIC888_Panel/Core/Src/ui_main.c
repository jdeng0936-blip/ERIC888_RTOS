/**
 * @file ui_main.c
 * ERIC888 Main UI — 快切装置操作界面
 *
 * Layout (800x480 dark theme):
 * ┌─────────────────────────────────────────────────┐
 * │ ERIC888 快切装置                    ETH GPS 4G  │ <- Title bar
 * ├───────────┬───────────┬─────────────────────────┤
 * │  Ua: 220V │  Ia: 5.2A │      状态: 正常运行     │
 * │  Ub: 221V │  Ib: 5.1A │      频率: 50.00 Hz     │
 * │  Uc: 219V │  Ic: 5.3A │                         │
 * ├───────────┴───────────┴─────────────────────────┤
 * │ [Tab1: 实时数据] [Tab2: 波形] [Tab3: 参数] [Tab4: 日志] │
 * └─────────────────────────────────────────────────┘
 */
#include "ui_main.h"
#include <stdio.h>

/* ---- Widget handles ---- */
static lv_obj_t *lbl_ua, *lbl_ub, *lbl_uc;
static lv_obj_t *lbl_ia, *lbl_ib, *lbl_ic;
static lv_obj_t *lbl_freq;
static lv_obj_t *lbl_status;
static lv_obj_t *led_eth, *led_4g, *led_gps;

/* Colors */
#define CLR_GREEN   lv_color_hex(0x00E676)
#define CLR_RED     lv_color_hex(0xFF5252)
#define CLR_YELLOW  lv_color_hex(0xFFD740)
#define CLR_BLUE    lv_color_hex(0x448AFF)
#define CLR_BG      lv_color_hex(0x1E1E2E)
#define CLR_CARD    lv_color_hex(0x2A2A3C)
#define CLR_TEXT    lv_color_hex(0xE0E0E0)

/* ---- Helper: create a card panel ---- */
static lv_obj_t *create_card(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, CLR_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    return card;
}

/* ---- Helper: create a value label ---- */
static lv_obj_t *create_value_label(lv_obj_t *parent, const char *title, lv_color_t color)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 2, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_title = lv_label_create(row);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x9E9E9E), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);

    lv_obj_t *lbl_val = lv_label_create(row);
    lv_label_set_text(lbl_val, "---");
    lv_obj_set_style_text_color(lbl_val, color, 0);
    lv_obj_set_style_text_font(lbl_val, &lv_font_montserrat_20, 0);

    return lbl_val;
}

/* ---- Title bar ---- */
static void create_title_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, 800, 50);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x141422), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 16, 0);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Title */
    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "ERIC888 " LV_SYMBOL_CHARGE " \xE5\xBF\xAB\xE5\x88\x87\xE8\xA3\x85\xE7\xBD\xAE");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    /* Network status LEDs */
    lv_obj_t *led_row = lv_obj_create(bar);
    lv_obj_set_size(led_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(led_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(led_row, 0, 0);
    lv_obj_set_style_pad_all(led_row, 0, 0);
    lv_obj_set_flex_flow(led_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(led_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(led_row, 8, 0);

    /* ETH LED */
    lv_obj_t *lbl_e = lv_label_create(led_row);
    lv_label_set_text(lbl_e, "ETH");
    lv_obj_set_style_text_color(lbl_e, CLR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_e, &lv_font_montserrat_14, 0);
    led_eth = lv_led_create(led_row);
    lv_led_set_color(led_eth, CLR_GREEN);
    lv_obj_set_size(led_eth, 12, 12);
    lv_led_off(led_eth);

    /* GPS LED */
    lv_obj_t *lbl_g = lv_label_create(led_row);
    lv_label_set_text(lbl_g, "GPS");
    lv_obj_set_style_text_color(lbl_g, CLR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_g, &lv_font_montserrat_14, 0);
    led_gps = lv_led_create(led_row);
    lv_led_set_color(led_gps, CLR_BLUE);
    lv_obj_set_size(led_gps, 12, 12);
    lv_led_off(led_gps);

    /* 4G LED */
    lv_obj_t *lbl_4 = lv_label_create(led_row);
    lv_label_set_text(lbl_4, "4G");
    lv_obj_set_style_text_color(lbl_4, CLR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_4, &lv_font_montserrat_14, 0);
    led_4g = lv_led_create(led_row);
    lv_led_set_color(led_4g, CLR_YELLOW);
    lv_obj_set_size(led_4g, 12, 12);
    lv_led_off(led_4g);
}

/**
 * Create the main ERIC888 UI
 */
void ui_main_create(void)
{
    /* Root screen */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);

    /* Title bar */
    create_title_bar(scr);

    /* Content area below title bar */
    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_set_size(content, 780, 410);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 55);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(content, 10, 0);

    /* ---- Voltage Card ---- */
    lv_obj_t *v_card = create_card(content, 220, 200);
    lv_obj_set_flex_flow(v_card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *v_title = lv_label_create(v_card);
    lv_label_set_text(v_title, LV_SYMBOL_CHARGE " Voltage");
    lv_obj_set_style_text_color(v_title, CLR_GREEN, 0);
    lv_obj_set_style_text_font(v_title, &lv_font_montserrat_14, 0);

    lbl_ua = create_value_label(v_card, "Ua", CLR_GREEN);
    lbl_ub = create_value_label(v_card, "Ub", CLR_GREEN);
    lbl_uc = create_value_label(v_card, "Uc", CLR_GREEN);

    /* ---- Current Card ---- */
    lv_obj_t *i_card = create_card(content, 220, 200);
    lv_obj_set_flex_flow(i_card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *i_title = lv_label_create(i_card);
    lv_label_set_text(i_title, LV_SYMBOL_LOOP " Current");
    lv_obj_set_style_text_color(i_title, CLR_YELLOW, 0);
    lv_obj_set_style_text_font(i_title, &lv_font_montserrat_14, 0);

    lbl_ia = create_value_label(i_card, "Ia", CLR_YELLOW);
    lbl_ib = create_value_label(i_card, "Ib", CLR_YELLOW);
    lbl_ic = create_value_label(i_card, "Ic", CLR_YELLOW);

    /* ---- Status Card ---- */
    lv_obj_t *s_card = create_card(content, 310, 200);
    lv_obj_set_flex_flow(s_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_card, 8, 0);

    lv_obj_t *s_title = lv_label_create(s_card);
    lv_label_set_text(s_title, LV_SYMBOL_SETTINGS " System");
    lv_obj_set_style_text_color(s_title, CLR_BLUE, 0);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_14, 0);

    /* Status label */
    lbl_status = lv_label_create(s_card);
    lv_label_set_text(lbl_status, "Status: Normal");
    lv_obj_set_style_text_color(lbl_status, CLR_GREEN, 0);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_20, 0);

    /* Frequency label */
    lbl_freq = lv_label_create(s_card);
    lv_label_set_text(lbl_freq, "Freq: 50.00 Hz");
    lv_obj_set_style_text_color(lbl_freq, CLR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_freq, &lv_font_montserrat_20, 0);
}

/* ---- Update functions ---- */

void ui_main_update_voltage(float ua, float ub, float uc)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f V", (double)ua); lv_label_set_text(lbl_ua, buf);
    snprintf(buf, sizeof(buf), "%.1f V", (double)ub); lv_label_set_text(lbl_ub, buf);
    snprintf(buf, sizeof(buf), "%.1f V", (double)uc); lv_label_set_text(lbl_uc, buf);
}

void ui_main_update_current(float ia, float ib, float ic)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f A", (double)ia); lv_label_set_text(lbl_ia, buf);
    snprintf(buf, sizeof(buf), "%.2f A", (double)ib); lv_label_set_text(lbl_ib, buf);
    snprintf(buf, sizeof(buf), "%.2f A", (double)ic); lv_label_set_text(lbl_ic, buf);
}

void ui_main_update_frequency(float freq)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "Freq: %.2f Hz", (double)freq);
    lv_label_set_text(lbl_freq, buf);
}

void ui_main_update_status(const char *text, lv_color_t color)
{
    lv_label_set_text(lbl_status, text);
    lv_obj_set_style_text_color(lbl_status, color, 0);
}

void ui_main_update_network(int eth_ok, int g4_ok, int gps_ok)
{
    if (eth_ok) lv_led_on(led_eth); else lv_led_off(led_eth);
    if (g4_ok)  lv_led_on(led_4g);  else lv_led_off(led_4g);
    if (gps_ok) lv_led_on(led_gps); else lv_led_off(led_gps);
}
