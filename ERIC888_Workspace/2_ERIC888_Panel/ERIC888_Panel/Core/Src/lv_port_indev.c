/**
 * @file lv_port_indev.c
 * LVGL input device driver for ERIC888 B-board
 * Touch: STMPE811 via I2C1
 */
#include "lv_port_indev.h"
#include "bsp_touch.h"

static lv_indev_drv_t indev_drv;

/**
 * Touch read callback for LVGL
 */
static void touchpad_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    Touch_Point pt;
    if (BSP_Touch_GetPoint(&pt) == 0) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = pt.x;
        data->point.y = pt.y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

/**
 * Initialize LVGL input device (touch)
 */
void lv_port_indev_init(void)
{
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read_cb;
    lv_indev_drv_register(&indev_drv);
}
