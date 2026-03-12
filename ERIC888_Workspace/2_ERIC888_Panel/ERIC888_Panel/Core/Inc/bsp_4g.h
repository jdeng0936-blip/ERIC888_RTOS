/**
 * @file    bsp_4g.h
 * @brief   EC20 4G module AT command driver for ERIC888 B-board
 *
 *          Hardware: UART7 (PF7=TX, PF6=RX), Power Control = PF8
 *          Protocol: AT command set (Quectel EC20 compatible)
 */
#ifndef BSP_4G_H
#define BSP_4G_H

#include "stm32f4xx_hal.h"

/* 4G module power control pin */
#define MOD4G_POW_PORT   GPIOF
#define MOD4G_POW_PIN    GPIO_PIN_8

/* Module status */
typedef struct {
    uint8_t  powered;        /* 1=powered on */
    uint8_t  registered;     /* 1=registered to network */
    uint8_t  signal_rssi;    /* signal strength (0-31, 99=unknown) */
    char     imei[20];       /* module IMEI */
    char     operator_name[32]; /* operator name */
} Mod4G_Status;

/**
 * @brief  Initialize 4G module (power on + AT check)
 * @param  huart: UART handle (UART7)
 * @retval 0=success, -1=fail
 */
int BSP_4G_Init(UART_HandleTypeDef *huart);

/**
 * @brief  Power on the 4G module
 */
void BSP_4G_PowerOn(void);

/**
 * @brief  Power off the 4G module
 */
void BSP_4G_PowerOff(void);

/**
 * @brief  Send AT command and wait for response
 * @param  cmd:        AT command string (e.g. "AT+CSQ\r\n")
 * @param  resp:       Response buffer
 * @param  resp_size:  Buffer size
 * @param  timeout_ms: Wait timeout
 * @retval 0=OK received, -1=ERROR or timeout
 */
int BSP_4G_SendCmd(const char *cmd, char *resp, uint16_t resp_size,
                   uint32_t timeout_ms);

/**
 * @brief  Check if module is registered to network
 * @param  status: output status struct
 * @retval 0=registered, -1=not registered
 */
int BSP_4G_GetStatus(Mod4G_Status *status);

/**
 * @brief  Query signal strength (AT+CSQ)
 * @retval RSSI value (0-31), or -1 on error
 */
int BSP_4G_GetSignal(void);

#endif /* BSP_4G_H */
