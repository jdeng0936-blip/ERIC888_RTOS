/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "task.h"
#include "eric888_spi_protocol.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_HandleTypeDef hdma_adc1;
extern DMA_HandleTypeDef hdma_spi4_rx;
extern DMA_HandleTypeDef hdma_spi4_tx;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim6;

/* USER CODE BEGIN EV */
extern volatile TaskHandle_t xCalcTaskHandle;
extern Eric888_DoubleBuffer g_adc_db;  /* ping-pong double buffer */
extern uint16_t adc_dma_buf[6];       /* internal ADC DMA buffer */

/* ISR latency monitor (DWT cycle counter, 180MHz = 5.56ns/tick) */
volatile uint32_t g_isr_cycles_last = 0;  /* last ISR cycles */
volatile uint32_t g_isr_cycles_max  = 0;  /* worst-case cycles */
volatile uint32_t g_isr_count       = 0;  /* total ISR calls */
extern Eric888_BatchRing g_batch_ring;     /* SDRAM batch ring */
/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32F4xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f4xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles EXTI line1 interrupt.
  */
void EXTI1_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI1_IRQn 0 */

  /* USER CODE END EXTI1_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(AD7606_BUSY_Pin);
  /* USER CODE BEGIN EXTI1_IRQn 1 */
  /*
   * AD7606 BUSY falling edge → data ready on FMC bus
   *
   * Critical ordering (25.6kHz, 35μs window):
   *   1. DWT start
   *   2. FMC read 8 channels (loop-unrolled, ~0.5μs)
   *   3. Snapshot internal ADC DMA (loop-unrolled)
   *   4. Push to batch ring FIRST (wr still valid)
   *   5. Swap double-buffer (wr becomes stale after this)
   *   6. If batch full → PI11 pulse + notify CalcTask
   *   7. DWT end
   */
  {
    uint32_t _cyc0 = DWT->CYCCNT;
    volatile int16_t *fmc_ad = (volatile int16_t *)0x6C000000;

    /* Get write buffer (valid until DB_Swap) */
    Eric888_ADC_Data *wr = Eric888_DB_GetWriteBuf(&g_adc_db);
    wr->timestamp_ms = HAL_GetTick();

    /* Step 2: FMC read — loop-unrolled for deterministic timing */
    wr->ch[0] = *fmc_ad; wr->ch[1] = *fmc_ad;
    wr->ch[2] = *fmc_ad; wr->ch[3] = *fmc_ad;
    wr->ch[4] = *fmc_ad; wr->ch[5] = *fmc_ad;
    wr->ch[6] = *fmc_ad; wr->ch[7] = *fmc_ad;

    /* Step 3: Snapshot internal ADC DMA — loop-unrolled */
    wr->internal_adc[0] = adc_dma_buf[0];
    wr->internal_adc[1] = adc_dma_buf[1];
    wr->internal_adc[2] = adc_dma_buf[2];
    wr->internal_adc[3] = adc_dma_buf[3];
    wr->internal_adc[4] = adc_dma_buf[4];
    wr->internal_adc[5] = adc_dma_buf[5];
    wr->sample_count++;

    /* Step 4: Push to batch ring BEFORE DB_Swap (wr is still valid!) */
    int batch_full = Eric888_Batch_Push(&g_batch_ring, wr);

    /* Step 5: Swap double-buffer (wr becomes stale pointer after this) */
    Eric888_DB_Swap(&g_adc_db);

    /* Step 6: If batch full → signal B-board + wake CalcTask */
    if (batch_full) {
      /* PI11 rising-edge pulse to notify B-board */
      GPIOI->BSRR = GPIO_PIN_11;            /* PI11 HIGH */
      __NOP(); __NOP(); __NOP(); __NOP();
      GPIOI->BSRR = GPIO_PIN_11 << 16;      /* PI11 LOW (pulse) */

      /* Notify CalcTask: full 512-sample batch ready for DSP */
      if (xCalcTaskHandle != NULL) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(xCalcTaskHandle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
      }
    }

    /* Step 7: Measure ISR execution time */
    g_isr_cycles_last = DWT->CYCCNT - _cyc0;
    if (g_isr_cycles_last > g_isr_cycles_max) g_isr_cycles_max = g_isr_cycles_last;
    g_isr_count++;
  }
  /* USER CODE END EXTI1_IRQn 1 */
}

/**
  * @brief This function handles TIM2 global interrupt.
  */
void TIM2_IRQHandler(void)
{
  /* USER CODE BEGIN TIM2_IRQn 0 */

  /* USER CODE END TIM2_IRQn 0 */
  HAL_TIM_IRQHandler(&htim2);
  /* USER CODE BEGIN TIM2_IRQn 1 */
  /* CONVST now driven by TIM3 CH1 hardware PWM - TIM2 ISR no longer needed */
  /* TIM2 kept as spare timer for future use */
  /* USER CODE END TIM2_IRQn 1 */
}

/**
  * @brief This function handles TIM6 global interrupt, DAC1 and DAC2 underrun error interrupts.
  */
void TIM6_DAC_IRQHandler(void)
{
  /* USER CODE BEGIN TIM6_DAC_IRQn 0 */

  /* USER CODE END TIM6_DAC_IRQn 0 */
  HAL_TIM_IRQHandler(&htim6);
  /* USER CODE BEGIN TIM6_DAC_IRQn 1 */

  /* USER CODE END TIM6_DAC_IRQn 1 */
}

/**
  * @brief This function handles DMA2 stream0 global interrupt.
  */
void DMA2_Stream0_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Stream0_IRQn 0 */

  /* USER CODE END DMA2_Stream0_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_spi4_rx);
  /* USER CODE BEGIN DMA2_Stream0_IRQn 1 */

  /* USER CODE END DMA2_Stream0_IRQn 1 */
}

/**
  * @brief This function handles DMA2 stream1 global interrupt.
  */
void DMA2_Stream1_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Stream1_IRQn 0 */

  /* USER CODE END DMA2_Stream1_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_spi4_tx);
  /* USER CODE BEGIN DMA2_Stream1_IRQn 1 */

  /* USER CODE END DMA2_Stream1_IRQn 1 */
}

/**
  * @brief This function handles DMA2 stream4 global interrupt.
  */
void DMA2_Stream4_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Stream4_IRQn 0 */

  /* USER CODE END DMA2_Stream4_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_adc1);
  /* USER CODE BEGIN DMA2_Stream4_IRQn 1 */

  /* USER CODE END DMA2_Stream4_IRQn 1 */
}

/* USER CODE BEGIN 1 */
/* USER CODE END 1 */
