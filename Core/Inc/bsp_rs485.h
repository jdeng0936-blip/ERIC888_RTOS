/**
 * @file    bsp_rs485.h
 * @brief   RS485 half-duplex driver (USART3 + PB8 TX enable)
 * @note    ERIC888 A-board - RS485 communication bus
 *
 *          USART3: PB10(TX), PB11(RX), 115200-8N1
 *          TX Enable: PB8 (HIGH = transmit, LOW = receive)
 */
#ifndef BSP_RS485_H
#define BSP_RS485_H

#include "stm32f4xx_hal.h"

/* RS485 TX enable pin */
#define RS485_TXEN_PORT   GPIOB
#define RS485_TXEN_PIN    GPIO_PIN_8

/**
 * @brief  Initialize RS485 driver (set to receive mode)
 */
void BSP_RS485_Init(void);

/**
 * @brief  Send data via RS485 (blocking)
 * @param  huart: UART handle (&huart3)
 * @param  data:  pointer to data buffer
 * @param  len:   number of bytes to send
 * @param  timeout_ms: transmit timeout
 * @retval 0=success, -1=fail
 */
int BSP_RS485_Send(UART_HandleTypeDef *huart,
                   uint8_t *data, uint16_t len, uint32_t timeout_ms);

/**
 * @brief  Start receiving data via RS485 (interrupt mode)
 * @param  huart: UART handle (&huart3)
 * @param  buf:   receive buffer
 * @param  len:   expected bytes
 * @retval 0=success, -1=fail
 */
int BSP_RS485_StartReceive(UART_HandleTypeDef *huart,
                           uint8_t *buf, uint16_t len);

#endif /* BSP_RS485_H */
