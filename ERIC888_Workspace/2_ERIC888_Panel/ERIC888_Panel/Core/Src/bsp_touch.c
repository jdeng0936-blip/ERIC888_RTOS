/**
 * @file    bsp_touch.c
 * @brief   STMPE811QTR I2C touch driver implementation
 *
 *          Polled-mode resistive touch reading via I2C1.
 *          Reads FIFO data for X/Y coordinates, maps from
 *          raw ADC (0~4095) to LCD pixels (800×480).
 */
#include "bsp_touch.h"

#define LCD_WIDTH   800
#define LCD_HEIGHT  480

static I2C_HandleTypeDef *s_hi2c;

/* ========================= I2C Register Access ========================= */

static HAL_StatusTypeDef reg_write(uint8_t reg, uint8_t val)
{
    return HAL_I2C_Mem_Write(s_hi2c, STMPE811_ADDR << 1, reg,
                             I2C_MEMADD_SIZE_8BIT, &val, 1, 100);
}

static uint8_t reg_read8(uint8_t reg)
{
    uint8_t val = 0;
    HAL_I2C_Mem_Read(s_hi2c, STMPE811_ADDR << 1, reg,
                     I2C_MEMADD_SIZE_8BIT, &val, 1, 100);
    return val;
}

static uint16_t reg_read16(uint8_t reg)
{
    uint8_t buf[2] = {0};
    HAL_I2C_Mem_Read(s_hi2c, STMPE811_ADDR << 1, reg,
                     I2C_MEMADD_SIZE_8BIT, buf, 2, 100);
    return (uint16_t)((buf[0] << 8) | buf[1]);
}

/* ========================= Public API ========================= */

int BSP_Touch_Init(I2C_HandleTypeDef *hi2c)
{
    s_hi2c = hi2c;

    /* Verify chip ID (should be 0x0811) */
    uint16_t chip_id = reg_read16(STMPE811_REG_CHIP_ID);
    if (chip_id != 0x0811) {
        return -1;  /* Chip not detected */
    }

    /* Software reset */
    reg_write(STMPE811_REG_SYS_CTRL1, 0x02);
    HAL_Delay(10);
    reg_write(STMPE811_REG_SYS_CTRL1, 0x00);
    HAL_Delay(2);

    /* Enable TSC and ADC clocks; disable GPIO clock (use all pins for TSC) */
    reg_write(STMPE811_REG_SYS_CTRL2, 0x04); /* Turn off GPIO clock */

    /* Configure ADC: 12-bit, internal ref, 80 clk cycles sample time */
    reg_write(STMPE811_REG_ADC_CTRL1, 0x49); /* 12-bit, 80 sample clk */
    HAL_Delay(2);
    reg_write(STMPE811_REG_ADC_CTRL2, 0x01); /* ADC clock = 3.25 MHz */

    /* Configure GPIO AF: all pins for TSC */
    reg_write(STMPE811_REG_GPIO_AF, 0x00);

    /* Configure Touch Screen Controller */
    reg_write(STMPE811_REG_TSC_CFG, 0x9A);   /* AVE=4, Touch Detect Delay=500us, Settling=500us */
    reg_write(STMPE811_REG_FIFO_TH, 0x01);   /* FIFO threshold = 1 */
    reg_write(STMPE811_REG_FIFO_STA, 0x01);  /* Reset FIFO */
    reg_write(STMPE811_REG_FIFO_STA, 0x00);  /* Unreset FIFO */

    /* Touch I-Drive: 50mA typical */
    reg_write(STMPE811_REG_TSC_I_DRIVE, 0x01);

    /* Enable TSC: XY acquisition mode, no window tracking */
    reg_write(STMPE811_REG_TSC_CTRL, 0x03);  /* Enable TSC, XY only */

    /* Clear any pending interrupts */
    reg_write(STMPE811_REG_INT_STA, 0xFF);

    return 0;
}

int BSP_Touch_GetPoint(Touch_Point *point)
{
    point->touched = 0;
    point->x = 0;
    point->y = 0;
    point->z = 0;

    /* Check if TSC status indicates touch */
    uint8_t ctrl = reg_read8(STMPE811_REG_TSC_CTRL);
    if (!(ctrl & 0x80)) {
        return -1;  /* No touch detected */
    }

    /* Check FIFO has data */
    uint8_t fifo_size = reg_read8(STMPE811_REG_FIFO_SIZE);
    if (fifo_size == 0) {
        return -1;
    }

    /* Read raw X/Y from data registers (last point) */
    uint16_t raw_x = reg_read16(STMPE811_REG_TSC_DATA_X);
    uint16_t raw_y = reg_read16(STMPE811_REG_TSC_DATA_Y);
    uint8_t  raw_z = reg_read8(STMPE811_REG_TSC_DATA_Z);

    /* Reset FIFO after reading */
    reg_write(STMPE811_REG_FIFO_STA, 0x01);
    reg_write(STMPE811_REG_FIFO_STA, 0x00);

    /* Map raw ADC (0~4095) to LCD pixel coordinates */
    point->x = (uint16_t)((uint32_t)raw_x * LCD_WIDTH / 4096);
    point->y = (uint16_t)((uint32_t)raw_y * LCD_HEIGHT / 4096);
    point->z = raw_z;
    point->touched = 1;

    /* Clamp to valid range */
    if (point->x >= LCD_WIDTH)  point->x = LCD_WIDTH - 1;
    if (point->y >= LCD_HEIGHT) point->y = LCD_HEIGHT - 1;

    return 0;
}

int BSP_Touch_IsTouched(void)
{
    uint8_t ctrl = reg_read8(STMPE811_REG_TSC_CTRL);
    return (ctrl & 0x80) ? 1 : 0;
}
