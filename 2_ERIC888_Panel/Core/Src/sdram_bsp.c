#include "main.h"

/* SDRAM Start Address */
#define SDRAM_DEVICE_ADDR  ((uint32_t)0xC0000000)
#define SDRAM_DEVICE_SIZE  ((uint32_t)0x800000)  /* 8 MB */

/* FMC SDRAM Bank 5 & 6 Command Register */
#define SDRAM_MODEREG_BURST_LENGTH_1             ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL      ((uint16_t)0x0000)
#define SDRAM_MODEREG_CAS_LATENCY_3              ((uint16_t)0x0030)
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD    ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE     ((uint16_t)0x0200)

SDRAM_HandleTypeDef hsdram1;

/**
  * @brief SDRAM Initialization Function
  * @param None
  * @retval None
  */
void MX_SDRAM_Init(void)
{
  FMC_SDRAM_TimingTypeDef SdramTiming = {0};

  /* Perform SDRAM initialization */
  hsdram1.Instance = FMC_SDRAM_DEVICE;
  /* hsdram1.Init configuration based on hardware schematic */
  /* Target: MT48LC4M32B2 or similar on Panel Board */
  hsdram1.Init.SDBank = FMC_SDRAM_BANK1;
  hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_8;
  hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
  hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
  hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
  hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
  hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
  hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_1;

  /* Timing configuration (HCLK = 180MHz, SDRAM clock = 90MHz) */
  SdramTiming.LoadToActiveDelay = 2;
  SdramTiming.ExitSelfRefreshDelay = 7;
  SdramTiming.SelfRefreshTime = 4;
  SdramTiming.RowCycleDelay = 7;
  SdramTiming.WriteRecoveryTime = 3;
  SdramTiming.RPDelay = 2;
  SdramTiming.RCDDelay = 2;

  if (HAL_SDRAM_Init(&hsdram1, &SdramTiming) != HAL_OK)
  {
    Error_Handler();
  }

  /* SDRAM sequence */
  FMC_SDRAM_CommandTypeDef command;
  
  /* 1. Clock enable */
  command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
  command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
  command.AutoRefreshNumber = 1;
  command.ModeRegisterDefinition = 0;
  HAL_SDRAM_SendCommand(&hsdram1, &command, 0xFFFF);
  HAL_Delay(1);

  /* 2. Precharge all */
  command.CommandMode = FMC_SDRAM_CMD_PALL;
  HAL_SDRAM_SendCommand(&hsdram1, &command, 0xFFFF);

  /* 3. Auto refresh */
  command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
  command.AutoRefreshNumber = 8;
  HAL_SDRAM_SendCommand(&hsdram1, &command, 0xFFFF);

  /* 4. Load Mode Register */
  uint32_t tmpmrd = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_1          |
                             SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL   |
                             SDRAM_MODEREG_CAS_LATENCY_3           |
                             SDRAM_MODEREG_OPERATING_MODE_STANDARD |
                             SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;
  command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
  command.ModeRegisterDefinition = tmpmrd;
  HAL_SDRAM_SendCommand(&hsdram1, &command, 0xFFFF);

  /* 5. Set refresh rate (64ms/4096 = 15.62us, 15.62us * 90MHz = 1406) */
  HAL_SDRAM_ProgramRefreshRate(&hsdram1, 1386); // Slightly lower for safety
}
