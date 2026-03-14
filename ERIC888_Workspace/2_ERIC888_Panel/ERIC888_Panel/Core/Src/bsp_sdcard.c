/**
 * @file bsp_sdcard.c
 * SDIO ↔ FatFs disk I/O adapter for ERIC888 B-board
 *
 * Features:
 *  - HAL_SD_ReadBlocks (polling, safe for FreeRTOS)
 *  - SD_CheckStatus wait mechanism to prevent HardFault
 *  - 4-byte aligned SDRAM buffer for DMA safety
 *  - Graceful handling of SD card not present
 */

#include "ff.h"
#include "diskio.h"
#include "sdio.h"
#include "stm32f4xx_hal.h"
#include <string.h>

extern SD_HandleTypeDef hsd;

/* ---- SD card presence flag ---- */
static volatile uint8_t sd_initialized = 0;

/**
 * 4-byte aligned SDRAM buffer for SDIO transfers.
 * SDIO DMA requires 4-byte aligned addresses.
 * 4KB buffer = 8 sectors, reduces SD physical reads.
 */
#define SDIO_BUF_SIZE  4096
static uint8_t sdio_align_buf[SDIO_BUF_SIZE] __attribute__((aligned(4)));

/**
 * Wait for SD card to be ready (not busy).
 * Prevents HardFault from reading during ongoing transfer.
 *
 * @param timeout_ms  Maximum wait time
 * @return 1 if ready, 0 if timeout
 */
static uint8_t SD_CheckStatus(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (HAL_SD_GetCardState(&hsd) == HAL_SD_CARD_TRANSFER) {
            return 1;  /* Ready */
        }
    }
    return 0;  /* Timeout — card busy or not present */
}

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    if (!sd_initialized) return STA_NOINIT;
    return 0;  /* OK */
}

/*-----------------------------------------------------------------------*/
/* Initialize a Drive                                                    */
/*-----------------------------------------------------------------------*/
DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;

    /* Check if SD card responds */
    if (HAL_SD_GetCardState(&hsd) == HAL_SD_CARD_TRANSFER) {
        sd_initialized = 1;
        return 0;
    }

    /* Try re-init (card may have been inserted after boot) */
    if (HAL_SD_Init(&hsd) == HAL_OK) {
        HAL_SD_ConfigWideBusOperation(&hsd, SDIO_BUS_WIDE_4B);
        if (HAL_SD_GetCardState(&hsd) == HAL_SD_CARD_TRANSFER) {
            sd_initialized = 1;
            return 0;
        }
    }

    sd_initialized = 0;
    return STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !sd_initialized) return RES_NOTRDY;

    /* Wait for card ready — prevents HardFault */
    if (!SD_CheckStatus(500)) return RES_ERROR;

    /* Check 4-byte alignment of destination buffer */
    if (((uint32_t)buff & 0x3) != 0) {
        /* Buffer not aligned — use aligned intermediate buffer */
        UINT sectors_done = 0;
        while (sectors_done < count) {
            UINT chunk = (count - sectors_done);
            if (chunk > (SDIO_BUF_SIZE / 512)) {
                chunk = SDIO_BUF_SIZE / 512;
            }

            if (!SD_CheckStatus(200)) return RES_ERROR;

            if (HAL_SD_ReadBlocks(&hsd,
                                  sdio_align_buf,
                                  sector + sectors_done,
                                  chunk,
                                  2000) != HAL_OK) {
                return RES_ERROR;
            }

            /* Wait for read to complete */
            if (!SD_CheckStatus(500)) return RES_ERROR;

            memcpy(buff + sectors_done * 512, sdio_align_buf, chunk * 512);
            sectors_done += chunk;
        }
        return RES_OK;
    }

    /* Buffer is aligned — direct read */
    if (HAL_SD_ReadBlocks(&hsd, buff, sector, count, 5000) != HAL_OK) {
        return RES_ERROR;
    }

    /* Wait for transfer complete */
    if (!SD_CheckStatus(500)) return RES_ERROR;

    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !sd_initialized) return RES_NOTRDY;

    if (!SD_CheckStatus(500)) return RES_ERROR;

    /* Check alignment */
    if (((uint32_t)buff & 0x3) != 0) {
        UINT sectors_done = 0;
        while (sectors_done < count) {
            UINT chunk = (count - sectors_done);
            if (chunk > (SDIO_BUF_SIZE / 512)) {
                chunk = SDIO_BUF_SIZE / 512;
            }
            memcpy(sdio_align_buf, buff + sectors_done * 512, chunk * 512);

            if (!SD_CheckStatus(200)) return RES_ERROR;

            if (HAL_SD_WriteBlocks(&hsd,
                                   sdio_align_buf,
                                   sector + sectors_done,
                                   chunk,
                                   2000) != HAL_OK) {
                return RES_ERROR;
            }
            if (!SD_CheckStatus(500)) return RES_ERROR;
            sectors_done += chunk;
        }
        return RES_OK;
    }

    if (HAL_SD_WriteBlocks(&hsd, (uint8_t *)buff, sector, count, 5000) != HAL_OK) {
        return RES_ERROR;
    }

    if (!SD_CheckStatus(500)) return RES_ERROR;

    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0 || !sd_initialized) return RES_NOTRDY;

    HAL_SD_CardInfoTypeDef info;

    switch (cmd) {
    case CTRL_SYNC:
        /* Wait until card is ready */
        if (SD_CheckStatus(1000)) return RES_OK;
        return RES_ERROR;

    case GET_SECTOR_COUNT:
        HAL_SD_GetCardInfo(&hsd, &info);
        *(LBA_t *)buff = info.LogBlockNbr;
        return RES_OK;

    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;

    case GET_BLOCK_SIZE:
        HAL_SD_GetCardInfo(&hsd, &info);
        *(DWORD *)buff = info.LogBlockSize / 512;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}

/*-----------------------------------------------------------------------*/
/* Get time for FatFs timestamps                                         */
/*-----------------------------------------------------------------------*/
DWORD get_fattime(void)
{
    /* Fixed time: 2026-01-01 00:00:00 */
    return ((DWORD)(2026 - 1980) << 25)
         | ((DWORD)1 << 21)
         | ((DWORD)1 << 16)
         | ((DWORD)0 << 11)
         | ((DWORD)0 << 5)
         | ((DWORD)0 >> 1);
}
