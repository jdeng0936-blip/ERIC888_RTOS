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
#include "bsp_spi_master.h"
#include "spi.h"
#include "iwdg.h"
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

/* Latest ADC data received from A-board (shared with Display/Network tasks) */
static Eric888_ADC_Data s_latest_adc;
static volatile uint8_t s_adc_fresh;

/* USER CODE END Variables */
osThreadId displayTaskHandle;
osThreadId commTaskHandle;
osThreadId networkTaskHandle;
osThreadId defaultTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void const * argument);
void StartCommTask(void const * argument);
void StartNetworkTask(void const * argument);

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
  /* definition and creation of displayTask */
  osThreadDef(displayTask, StartDefaultTask, osPriorityNormal, 0, 1024);
  displayTaskHandle = osThreadCreate(osThread(displayTask), NULL);

  /* definition and creation of commTask */
  osThreadDef(commTask, StartCommTask, osPriorityAboveNormal, 0, 512);
  commTaskHandle = osThreadCreate(osThread(commTask), NULL);

  /* definition and creation of networkTask */
  osThreadDef(networkTask, StartNetworkTask, osPriorityIdle, 0, 1024);
  networkTaskHandle = osThreadCreate(osThread(networkTask), NULL);

  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityLow, 0, 256);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the displayTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    /* TODO: LCD display refresh using s_latest_adc */
    /* TODO: Touch screen event handling */
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartCommTask */
/**
* @brief Function implementing the commTask thread.
*        SPI4 Master communication with A-board using v3.0 protocol.
*
*        Main loop:
*        1. Check PI11 batch-ready flag
*        2. If ready → SPI DMA read (CMD_READ_ADC)
*        3. Otherwise → periodic heartbeat polling
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartCommTask */
void StartCommTask(void const * argument)
{
  /* USER CODE BEGIN StartCommTask */
  /* Initialize SPI4 Master driver */
  BSP_SpiMaster_Init(&hspi4);

  uint32_t heartbeat_tick = 0;

  for(;;)
  {
    /* Check if A-board batch is ready (PI11 IRQ) */
    if (BSP_SpiMaster_IsBatchReady()) {
      BSP_SpiMaster_ClearBatchReady();

      /* Request latest ADC data from A-board */
      int ret = BSP_SpiMaster_Transfer(CMD_READ_ADC, NULL, 0);
      if (ret == 0) {
        /* Parse response payload into latest ADC data */
        const Eric888_SPI_Frame *rx = BSP_SpiMaster_GetRxFrame();
        if (rx->cmd == CMD_ACK && rx->len >= sizeof(Eric888_ADC_Data)) {
          memcpy(&s_latest_adc, rx->payload, sizeof(Eric888_ADC_Data));
          s_adc_fresh = 1;
        }
      }
    }

    /* Periodic heartbeat every 500ms */
    uint32_t now = osKernelSysTick();
    if ((now - heartbeat_tick) >= pdMS_TO_TICKS(500)) {
      heartbeat_tick = now;
      BSP_SpiMaster_Transfer(CMD_HEARTBEAT, NULL, 0);
    }

    osDelay(1);  /* 1ms loop rate */
  }
  /* USER CODE END StartCommTask */
}

/* USER CODE BEGIN Header_StartNetworkTask */
/**
* @brief Function implementing the networkTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartNetworkTask */
void StartNetworkTask(void const * argument)
{
  /* USER CODE BEGIN StartNetworkTask */
  /* Infinite loop */
  for(;;)
  {
    /* TODO: W5500 Ethernet communication */
    /* TODO: 4G module AT commands */
    /* TODO: GPS NMEA parsing */
    osDelay(10);
  }
  /* USER CODE END StartNetworkTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/**
 * @brief SPI TX/RX DMA complete callback
 *        Routes to BSP SPI Master driver
 */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI4) {
    BSP_SpiMaster_DmaComplete(hspi);
  }
}

/**
 * @brief GPIO EXTI callback
 *        PI11 = batch ready from A-board
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  BSP_SpiMaster_EXTI_Callback(GPIO_Pin);
}

/* USER CODE END Application */

