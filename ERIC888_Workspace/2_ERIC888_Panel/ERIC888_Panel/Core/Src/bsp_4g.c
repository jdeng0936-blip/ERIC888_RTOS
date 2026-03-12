/**
 * @file    bsp_4g.c
 * @brief   EC20 4G module AT command driver implementation
 */
#include "bsp_4g.h"
#include <string.h>
#include <stdio.h>

static UART_HandleTypeDef *s_huart;
static char s_rx_buf[256];

/* ========================= Private ========================= */

static int wait_for_response(char *resp, uint16_t resp_size, uint32_t timeout_ms)
{
    uint16_t idx = 0;
    uint32_t start = HAL_GetTick();

    memset(resp, 0, resp_size);

    while ((HAL_GetTick() - start) < timeout_ms) {
        uint8_t byte;
        if (HAL_UART_Receive(s_huart, &byte, 1, 10) == HAL_OK) {
            if (idx < resp_size - 1) {
                resp[idx++] = (char)byte;
            }
            /* Check for "OK" or "ERROR" terminator */
            if (idx >= 4) {
                if (strstr(resp, "OK\r\n") != NULL) return 0;
                if (strstr(resp, "ERROR") != NULL) return -1;
            }
        }
    }
    return -1;  /* Timeout */
}

/* ========================= Public API ========================= */

void BSP_4G_PowerOn(void)
{
    /* PF8 HIGH to enable 4G module power */
    HAL_GPIO_WritePin(MOD4G_POW_PORT, MOD4G_POW_PIN, GPIO_PIN_SET);
    HAL_Delay(500);  /* EC20 boot time ~500ms */
}

void BSP_4G_PowerOff(void)
{
    HAL_GPIO_WritePin(MOD4G_POW_PORT, MOD4G_POW_PIN, GPIO_PIN_RESET);
}

int BSP_4G_Init(UART_HandleTypeDef *huart)
{
    s_huart = huart;

    /* Power on module */
    BSP_4G_PowerOn();

    /* Wait for module to boot */
    HAL_Delay(3000);  /* EC20 needs ~3s to be ready */

    /* Test AT communication */
    char resp[128];
    for (int retry = 0; retry < 5; retry++) {
        if (BSP_4G_SendCmd("AT\r\n", resp, sizeof(resp), 1000) == 0) {
            /* Disable echo */
            BSP_4G_SendCmd("ATE0\r\n", resp, sizeof(resp), 1000);
            return 0;
        }
        HAL_Delay(500);
    }

    return -1;  /* Module not responding */
}

int BSP_4G_SendCmd(const char *cmd, char *resp, uint16_t resp_size,
                   uint32_t timeout_ms)
{
    /* Send command via UART */
    HAL_UART_Transmit(s_huart, (uint8_t *)cmd, strlen(cmd), 100);

    /* Wait for response */
    return wait_for_response(resp, resp_size, timeout_ms);
}

int BSP_4G_GetStatus(Mod4G_Status *status)
{
    char resp[128];

    memset(status, 0, sizeof(Mod4G_Status));
    status->powered = 1;

    /* Check registration: AT+CREG? → +CREG: 0,1 (registered home) */
    if (BSP_4G_SendCmd("AT+CREG?\r\n", resp, sizeof(resp), 2000) == 0) {
        char *p = strstr(resp, "+CREG:");
        if (p) {
            int n, stat;
            if (sscanf(p, "+CREG: %d,%d", &n, &stat) == 2) {
                status->registered = (stat == 1 || stat == 5) ? 1 : 0;
            }
        }
    }

    /* Signal strength: AT+CSQ → +CSQ: 18,0 */
    status->signal_rssi = (uint8_t)BSP_4G_GetSignal();

    /* IMEI: AT+GSN */
    if (BSP_4G_SendCmd("AT+GSN\r\n", resp, sizeof(resp), 2000) == 0) {
        /* Response: <IMEI>\r\n\r\nOK */
        char *p = resp;
        while (*p == '\r' || *p == '\n') p++;  /* skip leading CR/LF */
        int i = 0;
        while (*p && *p != '\r' && i < 19) {
            status->imei[i++] = *p++;
        }
        status->imei[i] = '\0';
    }

    return status->registered ? 0 : -1;
}

int BSP_4G_GetSignal(void)
{
    char resp[64];
    if (BSP_4G_SendCmd("AT+CSQ\r\n", resp, sizeof(resp), 2000) == 0) {
        char *p = strstr(resp, "+CSQ:");
        if (p) {
            int rssi, ber;
            if (sscanf(p, "+CSQ: %d,%d", &rssi, &ber) >= 1) {
                return rssi;
            }
        }
    }
    return -1;
}
