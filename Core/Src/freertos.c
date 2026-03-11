/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bsp_ad7606.h"
#include "bsp_adc.h"
#include "bsp_kswitch.h"
#include "bsp_rs485.h"
#include "bsp_sdram.h"
#include "adc.h"
#include "tim.h"
#include "usart.h"
#include "iwdg.h"
#include "eric888_spi_protocol.h"
#include "bsp_spi_slave.h"
#include "spi.h"
#include "dsp_calc.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern uint16_t adc_dma_buf[6];

/* Task notification handle - used by EXTI1 ISR to wake Calc task */
volatile TaskHandle_t xCalcTaskHandle = NULL;

/* Ping-pong double buffer: ISR writes, Task reads, zero-race */
Eric888_DoubleBuffer g_adc_db;

/* Batch ring buffer: 2 x 512 samples in SDRAM for SPI bulk transfer */
Eric888_BatchRing g_batch_ring __attribute__((section(".sdram")));
/* NOTE: if .sdram section is not defined in linker, place in BSS: */
/* Eric888_BatchRing g_batch_ring; */
/* USER CODE END Variables */
osThreadId Task_01Handle;
osThreadId myTask_CalcHandle;
osThreadId myTask_StoreHandle;
osThreadId myTask_MonitorHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartTask01(void const * argument);
void StartTaskCalc(void const * argument);
void StartTaskStore(void const * argument);
void StartTaskMonitor(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of Task_01 */
  osThreadDef(Task_01, StartTask01, osPriorityNormal, 0, 512);
  Task_01Handle = osThreadCreate(osThread(Task_01), NULL);

  /* definition and creation of myTask_Calc */
  osThreadDef(myTask_Calc, StartTaskCalc, osPriorityAboveNormal, 0, 1024);
  myTask_CalcHandle = osThreadCreate(osThread(myTask_Calc), NULL);

  /* definition and creation of myTask_Store */
  osThreadDef(myTask_Store, StartTaskStore, osPriorityAboveNormal, 0, 512);
  myTask_StoreHandle = osThreadCreate(osThread(myTask_Store), NULL);

  /* definition and creation of myTask_Monitor */
  osThreadDef(myTask_Monitor, StartTaskMonitor, osPriorityLow, 0, 256);
  myTask_MonitorHandle = osThreadCreate(osThread(myTask_Monitor), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_StartTask01 */
/**
  * @brief  Function implementing the Task_01 thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartTask01 */
void StartTask01(void const * argument)
{
  /* USER CODE BEGIN StartTask01 */
  /* Communication task: SPI4 inter-board + RS485 Modbus */
  BSP_RS485_Init();
  BSP_SpiSlave_Init(&hspi4);

  for(;;)
  {
    /* Refresh SPI TX mirror with latest ADC data from double-buffer */
    if (Eric888_DB_HasFresh(&g_adc_db)) {
      BSP_SpiSlave_RefreshTx(&g_adc_db);
    }

    /* TODO: Handle RS485 Modbus requests */
    osDelay(1);  /* 1ms refresh rate: B-board can poll at up to 1kHz */
  }
  /* USER CODE END StartTask01 */
}

/* USER CODE BEGIN Header_StartTaskCalc */
/**
* @brief Function implementing the myTask_Calc thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTaskCalc */
void StartTaskCalc(void const * argument)
{
  /* USER CODE BEGIN StartTaskCalc */
  /* Save handle for ISR notification */
  xCalcTaskHandle = xTaskGetCurrentTaskHandle();

  /* Initialize double-buffer */
  Eric888_DB_Init(&g_adc_db);
  Eric888_Batch_Init(&g_batch_ring);
  DSP_Init();

  for(;;)
  {
    /* Block until EXTI1 ISR notifies us (AD7606 data ready in ad7606_data[]) */
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    /* Check if batch ring read-side has a complete 512-sample block */
    if (g_batch_ring.read_ready) {
      const Eric888_BatchBank *batch = Eric888_Batch_GetReadBank(&g_batch_ring);
      const DSP_Results *dsp = DSP_ProcessBatch(batch->samples);
      /* Protection trip if needed */
      if (dsp->trip_requested) {
        BSP_KSwitch_TripAll();
      }
    }
  }
  /* USER CODE END StartTaskCalc */
}

/* USER CODE BEGIN Header_StartTaskStore */
/**
* @brief Function implementing the myTask_Store thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTaskStore */
void StartTaskStore(void const * argument)
{
  /* USER CODE BEGIN StartTaskStore */
  for(;;)
  {
    /* TODO: Wait for fault event queue from Calc task */
    /* TODO: Write waveform data to SDRAM ring buffer */
    osDelay(10);
  }
  /* USER CODE END StartTaskStore */
}

/* USER CODE BEGIN Header_StartTaskMonitor */
/**
* @brief Function implementing the myTask_Monitor thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTaskMonitor */
void StartTaskMonitor(void const * argument)
{
  /* USER CODE BEGIN StartTaskMonitor */
  for(;;)
  {
    /* Feed IWDG watchdog */
    HAL_IWDG_Refresh(&hiwdg);

    /* TODO: Toggle heartbeat LED */
    /* TODO: Report system status via SPI4 */
    osDelay(100);  /* 100ms watchdog feed interval */
  }
  /* USER CODE END StartTaskMonitor */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/**
 * @brief SPI TX/RX DMA complete callback
 *        Called when B-board completes a full-duplex SPI transaction
 */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI4) {
    BSP_SpiSlave_DmaComplete(hspi);
  }
}
/* USER CODE END Application */
