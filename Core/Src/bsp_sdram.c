/**
 * @file    bsp_sdram.c
 * @brief   W9825G6KH SDRAM init sequence and test
 * @note    Must call BSP_SDRAM_Init() after MX_FMC_Init()
 *
 *          Init steps (per W9825G6 datasheet):
 *          1. Wait >= 100us after power-up
 *          2. Precharge All banks
 *          3. At least 2x Auto Refresh
 *          4. Load Mode Register
 *          5. Configure refresh timer (64ms / 8192 rows)
 */
#include "bsp_sdram.h"
#include "fmc.h"
#include <string.h>

/* Refresh counter calculation:
 * SDRAM CLK = 90 MHz (HCLK/2)
 * Refresh period = 64 ms / 8192 rows = 7.81 us
 * COUNT = (7.81us * 90MHz) - 20 = 703 - 20 = 683
 */
#define SDRAM_REFRESH_COUNT    ((uint32_t)683)

int BSP_SDRAM_Init(SDRAM_HandleTypeDef *hsdram)
{
    FMC_SDRAM_CommandTypeDef cmd;

    /* Step 1: Clock Enable command, wait for stabilization */
    cmd.CommandMode            = FMC_SDRAM_CMD_CLK_ENABLE;
    cmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    cmd.AutoRefreshNumber      = 1;
    cmd.ModeRegisterDefinition = 0;

    if (HAL_SDRAM_SendCommand(hsdram, &cmd, 0xFFFF) != HAL_OK)
        return -1;

    /* Wait at least 100us (datasheet requirement) */
    HAL_Delay(1);  /* 1ms >> 100us, safe margin */

    /* Step 2: Precharge All banks */
    cmd.CommandMode            = FMC_SDRAM_CMD_PALL;
    cmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    cmd.AutoRefreshNumber      = 1;
    cmd.ModeRegisterDefinition = 0;

    if (HAL_SDRAM_SendCommand(hsdram, &cmd, 0xFFFF) != HAL_OK)
        return -1;

    /* Step 3: Auto Refresh (8 times, well above datasheet minimum of 2) */
    cmd.CommandMode            = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    cmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    cmd.AutoRefreshNumber      = 8;
    cmd.ModeRegisterDefinition = 0;

    if (HAL_SDRAM_SendCommand(hsdram, &cmd, 0xFFFF) != HAL_OK)
        return -1;

    /* Step 4: Load Mode Register */
    /* Burst Length=1, CAS Latency=3, Sequential, Single Write Burst */
    cmd.CommandMode            = FMC_SDRAM_CMD_LOAD_MODE;
    cmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    cmd.AutoRefreshNumber      = 1;
    cmd.ModeRegisterDefinition = (uint32_t)(
        SDRAM_MODEREG_BURST_LENGTH_1       |
        SDRAM_MODEREG_BURST_TYPE_SEQ       |
        SDRAM_MODEREG_CAS_LATENCY_3        |
        SDRAM_MODEREG_WRITEBURST_MODE_SINGLE
    );

    if (HAL_SDRAM_SendCommand(hsdram, &cmd, 0xFFFF) != HAL_OK)
        return -1;

    /* Step 5: Set refresh timer */
    if (HAL_SDRAM_ProgramRefreshRate(hsdram, SDRAM_REFRESH_COUNT) != HAL_OK)
        return -1;

    return 0;  /* Init success */
}

int BSP_SDRAM_Test(void)
{
    volatile uint32_t *pSDRAM = (volatile uint32_t *)SDRAM_BASE_ADDR;
    const uint32_t test_size  = 1024;  /* Test 1024 x 32-bit words = 4KB */
    uint32_t i;

    /* Write incremental data */
    for (i = 0; i < test_size; i++) {
        pSDRAM[i] = i;
    }

    /* Read back and verify */
    for (i = 0; i < test_size; i++) {
        if (pSDRAM[i] != i) {
            return -1;  /* Verify failed */
        }
    }

    /* Write inverted data */
    for (i = 0; i < test_size; i++) {
        pSDRAM[i] = ~i;
    }

    /* Read back and verify inverted */
    for (i = 0; i < test_size; i++) {
        if (pSDRAM[i] != ~i) {
            return -1;
        }
    }

    return 0;  /* All passed */
}
