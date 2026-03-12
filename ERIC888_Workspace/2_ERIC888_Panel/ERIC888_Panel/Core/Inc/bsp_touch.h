/**
 * @file    bsp_touch.h
 * @brief   STMPE811QTR Resistive Touch Controller driver (I2C1)
 *
 *          Hardware:
 *          - I2C1: PB8(SCL), PB9(SDA)
 *          - STMPE811 I2C Address: 0x82 (A0=GND → 7-bit = 0x41)
 *          - INT: (active low, optional — polled mode)
 *          - LCD panel: 800×480 resistive overlay
 */
#ifndef BSP_TOUCH_H
#define BSP_TOUCH_H

#include "stm32f4xx_hal.h"

/* STMPE811 I2C address (7-bit) */
#define STMPE811_ADDR           0x41

/* STMPE811 Register Map */
#define STMPE811_REG_CHIP_ID    0x00  /* Chip ID (0x0811) */
#define STMPE811_REG_SYS_CTRL1 0x03  /* System Control 1 (reset) */
#define STMPE811_REG_SYS_CTRL2 0x04  /* System Control 2 (clock) */
#define STMPE811_REG_INT_CTRL  0x09  /* Interrupt Control */
#define STMPE811_REG_INT_EN    0x0A  /* Interrupt Enable */
#define STMPE811_REG_INT_STA   0x0B  /* Interrupt Status */
#define STMPE811_REG_ADC_CTRL1 0x20  /* ADC Control 1 */
#define STMPE811_REG_ADC_CTRL2 0x21  /* ADC Control 2 */
#define STMPE811_REG_TSC_CTRL  0x40  /* Touch Screen Control */
#define STMPE811_REG_TSC_CFG   0x41  /* Touch Screen Config */
#define STMPE811_REG_FIFO_TH   0x4A  /* FIFO Threshold */
#define STMPE811_REG_FIFO_STA  0x4B  /* FIFO Status */
#define STMPE811_REG_FIFO_SIZE 0x4C  /* FIFO Size */
#define STMPE811_REG_TSC_DATA_X 0x4D /* Touch X data */
#define STMPE811_REG_TSC_DATA_Y 0x4F /* Touch Y data */
#define STMPE811_REG_TSC_DATA_Z 0x51 /* Touch Z (pressure) */
#define STMPE811_REG_TSC_I_DRIVE 0x58 /* Touch I-Drive */
#define STMPE811_REG_GPIO_AF   0x17  /* GPIO Alternate Function */

/* Touch point data */
typedef struct {
    uint16_t x;           /* X coordinate (0~4095 raw, mapped to 0~799) */
    uint16_t y;           /* Y coordinate (0~4095 raw, mapped to 0~479) */
    uint8_t  z;           /* Pressure (0=no touch) */
    uint8_t  touched;     /* 1=finger detected */
} Touch_Point;

/**
 * @brief  Initialize STMPE811 touch controller
 * @param  hi2c: I2C handle (I2C1)
 * @retval 0=success, -1=chip ID mismatch, -2=I2C error
 */
int BSP_Touch_Init(I2C_HandleTypeDef *hi2c);

/**
 * @brief  Read current touch point (polled mode)
 * @param  point: output touch data
 * @retval 0=touch detected, -1=no touch
 */
int BSP_Touch_GetPoint(Touch_Point *point);

/**
 * @brief  Check if screen is being touched
 * @retval 1=touched, 0=not touched
 */
int BSP_Touch_IsTouched(void);

#endif /* BSP_TOUCH_H */
