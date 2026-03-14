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
#include "i2c.h"
#include "usart.h"
#include "bsp_w5500.h"
#include "bsp_w5500_socket.h"
#include "bsp_4g.h"
#include "bsp_gps.h"
#include "bsp_touch.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "ui/ui.h"
#include "lv_fs_fatfs.h"
#include <stdio.h>
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
  /* Display + Watchdog + LVGL task */
  uint32_t led_cnt = 0;

  /* Initialize touch controller (STMPE811 on I2C1) */
  extern I2C_HandleTypeDef hi2c1;
  BSP_Touch_Init(&hi2c1);

  /* Initialize LVGL */
  lv_init();
  lv_port_disp_init();
  lv_port_indev_init();

  /* Mount SD card and register 'S:' LVGL filesystem for SquareLine images */
  lv_fs_fatfs_init();  /* Returns -1 if no SD card — UI still works, just no images */

  /* Create the ERIC888 UI (SquareLine Studio) */
  ui_init();

  uint32_t ui_update_tick = 0;

  for(;;)
  {
    /* Feed watchdog */
    HAL_IWDG_Refresh(&hiwdg);

    /* Status LED heartbeat (toggle every 500ms ~ 32 x 16ms) */
    if (++led_cnt >= 32) {
      led_cnt = 0;
      HAL_GPIO_TogglePin(GPIOH, GPIO_PIN_10); /* LED1 Run */
    }

    /* LVGL timer handler — drives rendering + input */
    lv_timer_handler();

    /* Update UI values from A-board data every 500ms */
    uint32_t now = HAL_GetTick();
    if ((now - ui_update_tick) >= 500) {
      ui_update_tick = now;
      if (s_adc_fresh) {
        char buf[16];
        /* Update SquareLine label widgets with real data */
        /* Voltage labels: ui_Label79(Ua), ui_Label80(Ub), ui_Label82(Uc) */
        snprintf(buf, sizeof(buf), "%.1f", (double)(s_latest_adc.ch[0] * 0.01f));
        lv_label_set_text(ui_Label79, buf);
        snprintf(buf, sizeof(buf), "%.1f", (double)(s_latest_adc.ch[1] * 0.01f));
        lv_label_set_text(ui_Label80, buf);
        snprintf(buf, sizeof(buf), "%.1f", (double)(s_latest_adc.ch[2] * 0.01f));
        lv_label_set_text(ui_Label82, buf);
        /* Current labels: ui_Label83(Ia), ui_Label84(Ib), ui_Label88(Ic) */
        snprintf(buf, sizeof(buf), "%.2f", (double)(s_latest_adc.ch[3] * 0.001f));
        lv_label_set_text(ui_Label83, buf);
        snprintf(buf, sizeof(buf), "%.2f", (double)(s_latest_adc.ch[4] * 0.001f));
        lv_label_set_text(ui_Label84, buf);
        snprintf(buf, sizeof(buf), "%.2f", (double)(s_latest_adc.ch[5] * 0.001f));
        lv_label_set_text(ui_Label88, buf);
      }
    }

    /* LED2 = PH11 (Fault) */
    if (s_adc_fresh && s_latest_adc.ch[0] == 0) {
      /* Placeholder: set fault LED based on actual fault flags */
    }

    osDelay(16); /* ~60 FPS LVGL refresh rate */
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
  /* Master-level fix: Bind current task for IRQ notification */
  BSP_SpiMaster_SetCommTask(xTaskGetCurrentTaskHandle());

  uint32_t heartbeat_tick = 0;

  for(;;)
  {
    /* Block until A-board PI11 IRQ notifies us (Batch ready) 
       Wait up to 100ms as fallback */
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)) > 0) {
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

  /* --- W5500 Ethernet Init --- */
  extern SPI_HandleTypeDef hspi2;
  int w5500_ok = BSP_W5500_Init(&hspi2);
  if (w5500_ok == 0) {
    /* Configure default network parameters */
    uint8_t ip[4]     = {192, 168, 1, 100};
    uint8_t gw[4]     = {192, 168, 1, 1};
    uint8_t subnet[4] = {255, 255, 255, 0};
    uint8_t mac[6]    = {0x00, 0x08, 0xDC, 0xEC, 0x88, 0x01};
    BSP_W5500_SetNetwork(ip, gw, subnet, mac);

    /* Open TCP server socket on port 502 (Modbus TCP) */
    W5500_Socket_Open(0, Sn_MR_TCP, 502);
    W5500_Socket_Listen(0);
  }

  /* --- 4G Module Init --- */
  extern UART_HandleTypeDef huart7;
  int mod4g_ok = BSP_4G_Init(&huart7);

  /* --- GPS UART1 Manual Init (PA9=TX, PA10=RX, 9600 baud) --- */
  UART_HandleTypeDef huart1_gps;
  huart1_gps.Instance = USART1;
  huart1_gps.Init.BaudRate = 9600;
  huart1_gps.Init.WordLength = UART_WORDLENGTH_8B;
  huart1_gps.Init.StopBits = UART_STOPBITS_1;
  huart1_gps.Init.Parity = UART_PARITY_NONE;
  huart1_gps.Init.Mode = UART_MODE_TX_RX;
  huart1_gps.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1_gps.Init.OverSampling = UART_OVERSAMPLING_16;

  /* Enable USART1 clock + GPIO */
  __HAL_RCC_USART1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  {
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);
  }
  HAL_UART_Init(&huart1_gps);
  BSP_GPS_Init(&huart1_gps);

  uint32_t link_check_tick = 0;
  uint32_t signal_check_tick = 0;

  for(;;)
  {
    uint32_t now = HAL_GetTick();

    /* W5500: check link + TCP socket state every 2s */
    if (w5500_ok == 0 && (now - link_check_tick) >= 2000) {
      link_check_tick = now;
      int link = BSP_W5500_GetLinkStatus();

      if (link) {
        /* Check TCP server socket 0 state */
        uint8_t sock_state = W5500_Socket_Status(0);
        if (sock_state == SOCK_ESTABLISHED) {
          /* Client connected — process Modbus TCP / data exchange */
          uint8_t rx_buf[256];
          int rx_len = W5500_Socket_Recv(0, rx_buf, sizeof(rx_buf));
          if (rx_len > 0) {
            /* TODO: Parse Modbus TCP request and respond */
            W5500_Socket_Send(0, rx_buf, (uint16_t)rx_len); /* Echo for now */
          }
        } else if (sock_state == SOCK_CLOSE_WAIT) {
          W5500_Socket_Close(0);
          W5500_Socket_Open(0, Sn_MR_TCP, 502);
          W5500_Socket_Listen(0);
        } else if (sock_state == SOCK_CLOSED) {
          W5500_Socket_Open(0, Sn_MR_TCP, 502);
          W5500_Socket_Listen(0);
        }
      }
    }

    /* 4G: check signal every 30s */
    if (mod4g_ok == 0 && (now - signal_check_tick) >= 30000) {
      signal_check_tick = now;
      Mod4G_Status status;
      BSP_4G_GetStatus(&status);
      /* LED3 = PH12: ON if 4G registered */
      HAL_GPIO_WritePin(GPIOH, GPIO_PIN_12,
                        status.registered ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }

    /* GPS: poll UART1 for NMEA bytes */
    {
      uint8_t byte;
      while (HAL_UART_Receive(&huart1_gps, &byte, 1, 1) == HAL_OK) {
        BSP_GPS_FeedByte(byte);
      }
    }

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
