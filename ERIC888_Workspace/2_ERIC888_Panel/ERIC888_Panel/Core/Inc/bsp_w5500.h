/**
 * @file    bsp_w5500.h
 * @brief   W5500 Ethernet driver for ERIC888 B-board
 *
 *          Hardware:
 *          - SPI2: PB10(SCK), PC2(MISO), PC3(MOSI), PB12(CS/NSS)
 *          - INT:  PF11
 *          - RST:  PC0
 *          - PHY:  HR911105A (integrated RJ45+transformer)
 *
 *          Uses WIZnet ioLibrary-style register access.
 *          Supports TCP server mode for Modbus TCP or IEC61850.
 */
#ifndef BSP_W5500_H
#define BSP_W5500_H

#include "stm32f4xx_hal.h"

/* W5500 SPI chip select */
#define W5500_CS_PORT   GPIOB
#define W5500_CS_PIN    GPIO_PIN_12

/* W5500 reset pin */
#define W5500_RST_PORT  GPIOC
#define W5500_RST_PIN   GPIO_PIN_0

/* W5500 interrupt pin */
#define W5500_INT_PORT  GPIOF
#define W5500_INT_PIN   GPIO_PIN_11

/* W5500 register addresses (Common Register Block) */
#define W5500_MR        0x0000  /* Mode Register */
#define W5500_GAR0      0x0001  /* Gateway Address */
#define W5500_SUBR0     0x0005  /* Subnet Mask */
#define W5500_SHAR0     0x0009  /* Source MAC */
#define W5500_SIPR0     0x000F  /* Source IP */
#define W5500_PHYCFGR   0x002E  /* PHY Config */
#define W5500_VERSIONR  0x0039  /* Version (should read 0x04) */

/* Control byte: BSB (Block Select Bits) + R/W */
#define W5500_CTRL_BSB_COMMON  (0x00 << 3)
#define W5500_CTRL_READ        (0x00 << 2)
#define W5500_CTRL_WRITE       (0x04)

/**
 * @brief  Initialize W5500 (hardware reset + SPI check)
 * @param  hspi: SPI handle (SPI2)
 * @retval 0=success, -1=fail (version mismatch)
 */
int BSP_W5500_Init(SPI_HandleTypeDef *hspi);

/**
 * @brief  Configure W5500 network parameters
 * @param  ip:      Source IP (4 bytes)
 * @param  gw:      Gateway (4 bytes)
 * @param  subnet:  Subnet mask (4 bytes)
 * @param  mac:     MAC address (6 bytes)
 */
void BSP_W5500_SetNetwork(const uint8_t *ip, const uint8_t *gw,
                          const uint8_t *subnet, const uint8_t *mac);

/**
 * @brief  Read W5500 PHY link status
 * @retval 1=link up, 0=link down
 */
int BSP_W5500_GetLinkStatus(void);

/**
 * @brief  Read a register from W5500
 */
uint8_t BSP_W5500_ReadReg(uint16_t addr, uint8_t bsb);

/**
 * @brief  Write a register to W5500
 */
void BSP_W5500_WriteReg(uint16_t addr, uint8_t bsb, uint8_t val);

/**
 * @brief  Read multiple bytes from W5500
 */
void BSP_W5500_ReadBuf(uint16_t addr, uint8_t bsb, uint8_t *buf, uint16_t len);

/**
 * @brief  Write multiple bytes to W5500
 */
void BSP_W5500_WriteBuf(uint16_t addr, uint8_t bsb, const uint8_t *buf, uint16_t len);

#endif /* BSP_W5500_H */
