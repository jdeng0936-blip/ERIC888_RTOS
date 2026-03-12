/**
 * @file    modbus_rtu.h
 * @brief   Modbus RTU Slave implementation for ERIC888 A-board
 *
 *          Supported function codes:
 *          - 0x03: Read Holding Registers (DSP results + status)
 *          - 0x06: Write Single Register  (protection thresholds)
 *
 *          Register map (addresses 0x0000 ~ 0x007F):
 *          ┌──────────────┬──────────────────────────────────┐
 *          │ Addr Range   │ Description                      │
 *          ├──────────────┼──────────────────────────────────┤
 *          │ 0x0000~0x0007│ RMS[0..7]  (uint16, mV)          │
 *          │ 0x0008~0x000F│ THD[0..7]  (uint16, 0.01%)       │
 *          │ 0x0010~0x0017│ Peak[0..7] (uint16, mV)          │
 *          │ 0x0018~0x001F│ Fund[0..7] (uint16, mV)          │
 *          │ 0x0020       │ fault_flags (uint16)              │
 *          │ 0x0021       │ trip_requested (uint16, 0 or 1)   │
 *          │ 0x0022       │ ISR cycles max (uint16)           │
 *          │ 0x0023       │ ISR count low16 (uint16)          │
 *          │ 0x0024       │ Fault record count (uint16)       │
 *          ├──────────────┼──────────────────────────────────┤
 *          │ 0x0040       │ OV threshold (uint16, mV)   [R/W]│
 *          │ 0x0041       │ UV threshold (uint16, mV)   [R/W]│
 *          │ 0x0042       │ OC threshold (uint16, mV)   [R/W]│
 *          │ 0x0043       │ THD threshold (uint16, 0.01%)[R/W]│
 *          │ 0x0044       │ Trip debounce (uint16, blocks)[R/W]│
 *          │ 0x0045       │ Slave address (uint16)      [R/W]│
 *          └──────────────┴──────────────────────────────────┘
 *
 *          Physical interface: USART3 (RS485 via PB10/PB11 + PB8 TXEN)
 */
#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include "stm32f4xx_hal.h"

/* Default slave address (configurable via register 0x0045) */
#define MODBUS_DEFAULT_ADDR  1

/* Register address ranges */
#define MODBUS_REG_RMS_BASE    0x0000
#define MODBUS_REG_THD_BASE    0x0008
#define MODBUS_REG_PEAK_BASE   0x0010
#define MODBUS_REG_FUND_BASE   0x0018
#define MODBUS_REG_FAULT_FLAGS 0x0020
#define MODBUS_REG_TRIP_REQ    0x0021
#define MODBUS_REG_ISR_MAX     0x0022
#define MODBUS_REG_ISR_COUNT   0x0023
#define MODBUS_REG_FAULT_COUNT 0x0024

#define MODBUS_REG_OV_THRESH   0x0040
#define MODBUS_REG_UV_THRESH   0x0041
#define MODBUS_REG_OC_THRESH   0x0042
#define MODBUS_REG_THD_THRESH  0x0043
#define MODBUS_REG_DEBOUNCE    0x0044
#define MODBUS_REG_SLAVE_ADDR  0x0045

/**
 * @brief  Initialize Modbus RTU slave
 * @param  huart: UART handle (USART3)
 */
void Modbus_Init(UART_HandleTypeDef *huart);

/**
 * @brief  Process one Modbus request/response cycle
 *         Blocks until a valid frame is received or timeout
 *         Call this in a loop from Task_01
 */
void Modbus_Poll(void);

#endif /* MODBUS_RTU_H */
