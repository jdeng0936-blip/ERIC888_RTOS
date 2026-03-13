/**
 * @file ui_main.h
 * ERIC888 Main UI — 快切装置操作界面
 */
#ifndef UI_MAIN_H
#define UI_MAIN_H

#include "lvgl.h"

/* Initialize the ERIC888 main UI */
void ui_main_create(void);

/* Update display values from A-board data */
void ui_main_update_voltage(float ua, float ub, float uc);
void ui_main_update_current(float ia, float ib, float ic);
void ui_main_update_frequency(float freq);
void ui_main_update_status(const char *status_text, lv_color_t color);
void ui_main_update_network(int eth_ok, int g4_ok, int gps_ok);

#endif /* UI_MAIN_H */
