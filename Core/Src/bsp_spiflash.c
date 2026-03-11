/**
 * @file    bsp_spiflash.c
 * @brief   SPI Flash (W25Qxx) driver implementation
 *
 *          SPI3 is initialized manually here (not via CubeMX) to avoid
 *          Makefile regeneration issues. GPIO pins are already configured
 *          by CubeMX in gpio.c (PB3/PB4 as AF6_SPI3, PD6 as AF5_SPI3).
 */
#include "bsp_spiflash.h"
#include <string.h>

/* SPI3 handle (initialized by CubeMX MX_SPI3_Init) */
extern SPI_HandleTypeDef hspi3;

/* Internal helpers */
static inline void flash_cs_low(void)  { HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET); }
static inline void flash_cs_high(void) { HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET); }

static void flash_write_enable(void)
{
    uint8_t cmd = W25Q_CMD_WRITE_ENABLE;
    flash_cs_low();
    HAL_SPI_Transmit(&hspi3, &cmd, 1, 100);
    flash_cs_high();
}

static int flash_wait_busy(uint32_t timeout_ms)
{
    uint8_t cmd = W25Q_CMD_READ_STATUS1;
    uint8_t status;
    uint32_t start = HAL_GetTick();

    do {
        flash_cs_low();
        HAL_SPI_Transmit(&hspi3, &cmd, 1, 100);
        HAL_SPI_Receive(&hspi3, &status, 1, 100);
        flash_cs_high();

        if (!(status & W25Q_STATUS_BUSY)) {
            return 0;
        }
    } while ((HAL_GetTick() - start) < timeout_ms);

    return -1;  /* Timeout */
}

int BSP_SpiFlash_Init(void)
{
    /* SPI3 already initialized by CubeMX MX_SPI3_Init() */
    /* Just ensure CS is HIGH (deselect) */
    flash_cs_high();
    return 0;
}

uint32_t BSP_SpiFlash_ReadID(void)
{
    uint8_t cmd = W25Q_CMD_JEDEC_ID;
    uint8_t id[3];

    flash_cs_low();
    HAL_SPI_Transmit(&hspi3, &cmd, 1, 100);
    HAL_SPI_Receive(&hspi3, id, 3, 100);
    flash_cs_high();

    return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
}

void BSP_SpiFlash_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint8_t cmd[4];
    cmd[0] = W25Q_CMD_READ_DATA;
    cmd[1] = (addr >> 16) & 0xFF;
    cmd[2] = (addr >> 8) & 0xFF;
    cmd[3] = addr & 0xFF;

    flash_cs_low();
    HAL_SPI_Transmit(&hspi3, cmd, 4, 100);
    HAL_SPI_Receive(&hspi3, buf, len, 1000);
    flash_cs_high();
}

int BSP_SpiFlash_WritePage(uint32_t addr, uint8_t *buf, uint16_t len)
{
    if (len > 256) len = 256;  /* W25Q page size = 256 bytes */

    uint8_t cmd[4];
    cmd[0] = W25Q_CMD_PAGE_PROGRAM;
    cmd[1] = (addr >> 16) & 0xFF;
    cmd[2] = (addr >> 8) & 0xFF;
    cmd[3] = addr & 0xFF;

    flash_write_enable();

    flash_cs_low();
    HAL_SPI_Transmit(&hspi3, cmd, 4, 100);
    HAL_SPI_Transmit(&hspi3, buf, len, 1000);
    flash_cs_high();

    /* Wait for write to complete (typical 0.7ms, max 3ms) */
    return flash_wait_busy(10);
}

int BSP_SpiFlash_EraseSector(uint32_t addr)
{
    uint8_t cmd[4];
    cmd[0] = W25Q_CMD_SECTOR_ERASE;
    cmd[1] = (addr >> 16) & 0xFF;
    cmd[2] = (addr >> 8) & 0xFF;
    cmd[3] = addr & 0xFF;

    flash_write_enable();

    flash_cs_low();
    HAL_SPI_Transmit(&hspi3, cmd, 4, 100);
    flash_cs_high();

    /* Wait for erase to complete (typical 45ms, max 400ms) */
    return flash_wait_busy(500);
}
