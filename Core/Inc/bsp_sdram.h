/**
 * @file    bsp_sdram.h
 * @brief   W9825G6KH SDRAM driver (FMC Bank1)
 * @note    ERIC888 A-board - 256Mbit (32MB) SDRAM
 */
#ifndef BSP_SDRAM_H
#define BSP_SDRAM_H

#include "stm32f4xx_hal.h"

/* SDRAM base address (FMC Bank1, SDRAM Bank1 = 0xC0000000) */
#define SDRAM_BASE_ADDR       ((uint32_t)0xC0000000)
#define SDRAM_SIZE            ((uint32_t)0x02000000)  /* 32 MB */

/* W9825G6 Mode Register bits */
#define SDRAM_MODEREG_BURST_LENGTH_1       ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_LENGTH_2       ((uint16_t)0x0001)
#define SDRAM_MODEREG_BURST_LENGTH_4       ((uint16_t)0x0002)
#define SDRAM_MODEREG_BURST_LENGTH_8       ((uint16_t)0x0003)
#define SDRAM_MODEREG_BURST_TYPE_SEQ       ((uint16_t)0x0000)
#define SDRAM_MODEREG_CAS_LATENCY_2        ((uint16_t)0x0020)
#define SDRAM_MODEREG_CAS_LATENCY_3        ((uint16_t)0x0030)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE ((uint16_t)0x0200)

/**
 * @brief  SDRAM full init sequence
 * @param  hsdram: SDRAM handle pointer
 * @retval 0=success, -1=fail
 */
int BSP_SDRAM_Init(SDRAM_HandleTypeDef *hsdram);

/**
 * @brief  SDRAM read/write test (incremental data)
 * @retval 0=pass, -1=fail
 */
int BSP_SDRAM_Test(void);

#endif /* BSP_SDRAM_H */
