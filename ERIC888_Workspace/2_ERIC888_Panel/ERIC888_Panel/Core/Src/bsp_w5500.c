/**
 * @file    bsp_w5500.c
 * @brief   W5500 Ethernet SPI driver implementation
 *
 *          Uses SPI2 with software CS (PB12) for W5500 register access.
 *          Basic register read/write and network configuration.
 */
#include "bsp_w5500.h"
#include <string.h>

static SPI_HandleTypeDef *s_hspi;

/* ========================= Internal ========================= */

static inline void cs_low(void)  { HAL_GPIO_WritePin(W5500_CS_PORT, W5500_CS_PIN, GPIO_PIN_RESET); }
static inline void cs_high(void) { HAL_GPIO_WritePin(W5500_CS_PORT, W5500_CS_PIN, GPIO_PIN_SET); }

/* ========================= Public API ========================= */

int BSP_W5500_Init(SPI_HandleTypeDef *hspi)
{
    s_hspi = hspi;

    /* Ensure CS is high (deselect) */
    cs_high();

    /* Hardware reset */
    HAL_GPIO_WritePin(W5500_RST_PORT, W5500_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(2);
    HAL_GPIO_WritePin(W5500_RST_PORT, W5500_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(10);  /* W5500 needs ~5ms after reset */

    /* Verify chip version (should read 0x04) */
    uint8_t ver = BSP_W5500_ReadReg(W5500_VERSIONR, W5500_CTRL_BSB_COMMON);
    if (ver != 0x04) {
        return -1;  /* W5500 not detected or SPI error */
    }

    /* Software reset */
    BSP_W5500_WriteReg(W5500_MR, W5500_CTRL_BSB_COMMON, 0x80);
    HAL_Delay(10);

    return 0;
}

void BSP_W5500_SetNetwork(const uint8_t *ip, const uint8_t *gw,
                          const uint8_t *subnet, const uint8_t *mac)
{
    BSP_W5500_WriteBuf(W5500_SIPR0, W5500_CTRL_BSB_COMMON, ip, 4);
    BSP_W5500_WriteBuf(W5500_GAR0,  W5500_CTRL_BSB_COMMON, gw, 4);
    BSP_W5500_WriteBuf(W5500_SUBR0, W5500_CTRL_BSB_COMMON, subnet, 4);
    BSP_W5500_WriteBuf(W5500_SHAR0, W5500_CTRL_BSB_COMMON, mac, 6);
}

int BSP_W5500_GetLinkStatus(void)
{
    uint8_t phy = BSP_W5500_ReadReg(W5500_PHYCFGR, W5500_CTRL_BSB_COMMON);
    return (phy & 0x01) ? 1 : 0;  /* bit0 = link status */
}

uint8_t BSP_W5500_ReadReg(uint16_t addr, uint8_t bsb)
{
    uint8_t cmd[3];
    uint8_t val;

    cmd[0] = (addr >> 8) & 0xFF;
    cmd[1] = addr & 0xFF;
    cmd[2] = (bsb | W5500_CTRL_READ);  /* Read mode */

    cs_low();
    HAL_SPI_Transmit(s_hspi, cmd, 3, 100);
    HAL_SPI_Receive(s_hspi, &val, 1, 100);
    cs_high();

    return val;
}

void BSP_W5500_WriteReg(uint16_t addr, uint8_t bsb, uint8_t val)
{
    uint8_t cmd[3];

    cmd[0] = (addr >> 8) & 0xFF;
    cmd[1] = addr & 0xFF;
    cmd[2] = (bsb | W5500_CTRL_WRITE);

    cs_low();
    HAL_SPI_Transmit(s_hspi, cmd, 3, 100);
    HAL_SPI_Transmit(s_hspi, &val, 1, 100);
    cs_high();
}

void BSP_W5500_ReadBuf(uint16_t addr, uint8_t bsb, uint8_t *buf, uint16_t len)
{
    uint8_t cmd[3];

    cmd[0] = (addr >> 8) & 0xFF;
    cmd[1] = addr & 0xFF;
    cmd[2] = (bsb | W5500_CTRL_READ);

    cs_low();
    HAL_SPI_Transmit(s_hspi, cmd, 3, 100);
    HAL_SPI_Receive(s_hspi, buf, len, 500);
    cs_high();
}

void BSP_W5500_WriteBuf(uint16_t addr, uint8_t bsb, const uint8_t *buf, uint16_t len)
{
    uint8_t cmd[3];

    cmd[0] = (addr >> 8) & 0xFF;
    cmd[1] = addr & 0xFF;
    cmd[2] = (bsb | W5500_CTRL_WRITE);

    cs_low();
    HAL_SPI_Transmit(s_hspi, cmd, 3, 100);
    HAL_SPI_Transmit(s_hspi, (uint8_t *)buf, len, 500);
    cs_high();
}
