#include "bsp_sdram.h"

SDRAM_HandleTypeDef hsdram1;

/**
  * @brief SDRAM Initialization Function
  * @param None
  * @retval None
  */
void SDRAM_Init(void)
{
    FMC_SDRAM_TimingTypeDef SdramTiming = {0};

    hsdram1.Instance = FMC_SDRAM_DEVICE;
    /* hsdram1.Init */
    hsdram1.Init.SDRBank = FMC_SDRAM_BANK2; // W9825G6KH-6 normally on Bank 2 for 0xC0000000
    hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_9;
    hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_13;
    hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
    hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
    hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
    hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2; // 180MHz/2 = 90MHz
    hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;     // Optimized per MEMORY.md
    hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_1;

    /* SdramTiming */
    /* 90MHz = 11.11ns per clock cycle */
    /* W9825G6KH-6 Requirements (at 166MHz/6ns, but we use 90MHz/11.1ns) */
    SdramTiming.LoadToActiveDelay = 2;    // TMRD: 2 cycles
    SdramTiming.ExitSelfRefreshDelay = 7; // TXSR: 70ns / 11.1ns ~ 6.3
    SdramTiming.SelfRefreshTime = 4;      // TRAS: 42ns / 11.1ns ~ 3.7
    SdramTiming.RowCycleDelay = 6;         // TRC:  60ns / 11.1ns ~ 5.4
    SdramTiming.WriteRecoveryTime = 2;    // TWR:  2 cycles
    SdramTiming.RPDelay = 2;              // TRP:  18ns / 11.1ns ~ 1.6
    SdramTiming.RCDDelay = 2;             // TRCD: 18ns / 11.1ns ~ 1.6

    if (HAL_SDRAM_Init(&hsdram1, &SdramTiming) != HAL_OK)
    {
        /* Initialization Error */
        while(1);
    }

    /* SDRAM sequence */
    FMC_SDRAM_CommandTypeDef command;

    /* 1. CLK enable */
    command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
    command.AutoRefreshNumber = 1;
    command.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(&hsdram1, &command, 0xFFFF);
    HAL_Delay(1);

    /* 2. Precharge All */
    command.CommandMode = FMC_SDRAM_CMD_PALL;
    HAL_SDRAM_SendCommand(&hsdram1, &command, 0xFFFF);

    /* 3. Auto Refresh x8 */
    command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    command.AutoRefreshNumber = 8;
    HAL_SDRAM_SendCommand(&hsdram1, &command, 0xFFFF);

    /* 4. Load Mode Register */
    /* W9825G6KH: CAS=3, Burst=1 (LCD FrameBuffer usually sequential if needed, but LTDC handles it) */
    uint32_t tmpmrd = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_1          |
                     SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL   |
                     SDRAM_MODEREG_CAS_LATENCY_3           |
                     SDRAM_MODEREG_OPERATING_MODE_STANDARD |
                     SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

    command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    command.ModeRegisterDefinition = tmpmrd;
    HAL_SDRAM_SendCommand(&hsdram1, &command, 0xFFFF);

    /* 5. Set Refresh Rate */
    /* 64ms / 8192 rows = 7.8125 us */
    /* 7.8125us * 90MHz = 703 cycles. Subtract 20 for safety margin as per ST docs = 683 */
    HAL_SDRAM_ProgramRefreshRate(&hsdram1, 683);
}

/**
  * @brief FMC MSP Initialization
  * This function configures the hardware resources used in this example
  * @param hsdram: SDRAM handle
  * @retval None
  */
void HAL_SDRAM_MspInit(SDRAM_HandleTypeDef* hsdram)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if (hsdram->Instance == FMC_SDRAM_DEVICE) {
    /* Peripheral clock enable */
    __HAL_RCC_FMC_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    /** FMC GPIO Configuration
    PF0   ------> FMC_A0
    PF1   ------> FMC_A1
    PF2   ------> FMC_A2
    PF3   ------> FMC_A3
    PF4   ------> FMC_A4
    PF5   ------> FMC_A5
    PF11  ------> FMC_SDNRAS
    PF12  ------> FMC_A6
    PF13  ------> FMC_A7
    PF14  ------> FMC_A8
    PF15  ------> FMC_A9
    PG0   ------> FMC_A10
    PG1   ------> FMC_A11
    PG4   ------> FMC_BA0
    PG5   ------> FMC_BA1
    PG8   ------> FMC_SDCLK
    PG15  ------> FMC_SDNCAS
    PH2   ------> FMC_SDCKE0
    PH3   ------> FMC_SDNE0
    PH5   ------> FMC_SDNWE
    PD0   ------> FMC_D2
    PD1   ------> FMC_D3
    PD8   ------> FMC_D13
    PD9   ------> FMC_D14
    PD10  ------> FMC_D15
    PD14  ------> FMC_D0
    PD15  ------> FMC_D1
    PE0   ------> FMC_NBL0
    PE1   ------> FMC_NBL1
    PE7   ------> FMC_D4
    PE8   ------> FMC_D5
    PE9   ------> FMC_D6
    PE10  ------> FMC_D7
    PE11  ------> FMC_D8
    PE12  ------> FMC_D9
    PE13  ------> FMC_D10
    PE14  ------> FMC_D11
    PE15  ------> FMC_D12
    */
    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_11|GPIO_PIN_12
                          |GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_5
                          |GPIO_PIN_8|GPIO_PIN_15;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_5;
    HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_8|GPIO_PIN_9
                          |GPIO_PIN_10|GPIO_PIN_14|GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_7|GPIO_PIN_8
                          |GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12
                          |GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
  }
}
