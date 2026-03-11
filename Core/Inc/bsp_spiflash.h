/**
 * @file    bsp_spiflash.h
 * @brief   SPI Flash (W25Qxx) driver via SPI3
 * @note    ERIC888 A-board - parameter/waveform storage
 *
 *          SPI3 pins (already configured as AF in gpio.c):
 *          SCK  = PB3  (AF6_SPI3)
 *          MISO = PB4  (AF6_SPI3)
 *          MOSI = PD6  (AF5_SPI3)
 *          CS   = PA4  (GPIO Output, active LOW)
 */
#ifndef BSP_SPIFLASH_H
#define BSP_SPIFLASH_H

#include "stm32f4xx_hal.h"

/* Flash CS pin */
#define FLASH_CS_PORT     GPIOA
#define FLASH_CS_PIN      GPIO_PIN_4

/* W25Q common commands */
#define W25Q_CMD_WRITE_ENABLE   0x06
#define W25Q_CMD_READ_STATUS1   0x05
#define W25Q_CMD_READ_DATA      0x03
#define W25Q_CMD_PAGE_PROGRAM   0x02
#define W25Q_CMD_SECTOR_ERASE   0x20
#define W25Q_CMD_JEDEC_ID       0x9F

/* Status register bits */
#define W25Q_STATUS_BUSY        0x01

/**
 * @brief  Initialize SPI3 peripheral and Flash CS pin
 * @retval 0=success, -1=fail
 */
int BSP_SpiFlash_Init(void);

/**
 * @brief  Read JEDEC ID (manufacturer + device type + capacity)
 * @retval 24-bit JEDEC ID (e.g. 0xEF4018 for W25Q128)
 */
uint32_t BSP_SpiFlash_ReadID(void);

/**
 * @brief  Read data from flash
 * @param  addr: 24-bit start address
 * @param  buf:  destination buffer
 * @param  len:  number of bytes to read
 */
void BSP_SpiFlash_Read(uint32_t addr, uint8_t *buf, uint32_t len);

/**
 * @brief  Write data to flash (auto page-program, must erase first)
 * @param  addr: 24-bit start address (must be page-aligned for best results)
 * @param  buf:  source data
 * @param  len:  number of bytes (max 256 per page)
 * @retval 0=success, -1=timeout
 */
int BSP_SpiFlash_WritePage(uint32_t addr, uint8_t *buf, uint16_t len);

/**
 * @brief  Erase a 4KB sector
 * @param  addr: any address within the sector
 * @retval 0=success, -1=timeout
 */
int BSP_SpiFlash_EraseSector(uint32_t addr);

#endif /* BSP_SPIFLASH_H */
