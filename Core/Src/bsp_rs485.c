/**
 * @file    bsp_rs485.c
 * @brief   RS485 half-duplex driver implementation
 *
 *          TX flow: set TXEN HIGH -> transmit -> wait complete -> set TXEN LOW
 *          RX flow: TXEN stays LOW (receive mode is default)
 */
#include "bsp_rs485.h"

void BSP_RS485_Init(void)
{
    /* Default to receive mode: TXEN = LOW */
    HAL_GPIO_WritePin(RS485_TXEN_PORT, RS485_TXEN_PIN, GPIO_PIN_RESET);
}

int BSP_RS485_Send(UART_HandleTypeDef *huart,
                   uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    HAL_StatusTypeDef status;

    /* Switch to transmit mode */
    HAL_GPIO_WritePin(RS485_TXEN_PORT, RS485_TXEN_PIN, GPIO_PIN_SET);

    /* Transmit data (blocking) */
    status = HAL_UART_Transmit(huart, data, len, timeout_ms);

    /* Switch back to receive mode */
    HAL_GPIO_WritePin(RS485_TXEN_PORT, RS485_TXEN_PIN, GPIO_PIN_RESET);

    return (status == HAL_OK) ? 0 : -1;
}

int BSP_RS485_StartReceive(UART_HandleTypeDef *huart,
                           uint8_t *buf, uint16_t len)
{
    /* Ensure we are in receive mode */
    HAL_GPIO_WritePin(RS485_TXEN_PORT, RS485_TXEN_PIN, GPIO_PIN_RESET);

    /* Start interrupt-based receive */
    if (HAL_UART_Receive_IT(huart, buf, len) != HAL_OK) {
        return -1;
    }

    return 0;
}
