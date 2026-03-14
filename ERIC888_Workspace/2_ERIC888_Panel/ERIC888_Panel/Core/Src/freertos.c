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
#include "ui_fts_status.h"
#include "modbus_tcp.h"
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

/* ✅ [Embedded-Engineer] 最新 FTS 保护状态快照 (A 板 → SPI4 → B 板) */
static FTS_StatusSnapshot_t s_latest_fts;
static volatile uint8_t s_fts_fresh;

/*
 * ✅ [Fix #2] 跨任务数据互斥锁
 * CommTask(写) 和 DisplayTask(读) 共享 s_latest_adc / s_latest_fts。
 * 结构体 memcpy 非原子操作 → 用 mutex 保护原子性。
 */
static SemaphoreHandle_t s_data_mutex;
static StaticSemaphore_t s_data_mutex_buf;

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
void StartIdleMaintenanceTask(void const * argument);

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
  /* [Fix #2] 创建数据互斥锁 (静态分配) */
  s_data_mutex = xSemaphoreCreateMutexStatic(&s_data_mutex_buf);
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
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

  /*
   * [Fix #1] defaultTask 改绑独立空闲维护函数
   *
   * 原 BUG: defaultTask 也绑 StartDefaultTask -> LVGL 双任务并发 -> HardFault
   * 修复: 改为 StartIdleMaintenanceTask (仅做栈水位监控)
   */
  osThreadDef(defaultTask, StartIdleMaintenanceTask, osPriorityLow, 0, 256);
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

  /* ✅ [Embedded-Engineer] 在 Tab1 中创建 FTS 保护状态面板 */
  ui_fts_create(ui_Tab1);

  /* Load GB2312 fonts from SD card and set as fallback for UI fonts */
  /* Must be after ui_init() so font objects exist, and after lv_fs_fatfs_init() */
  /* so the 'S:' drive is available for reading font files */
  lv_fs_load_sd_fonts();  /* Returns 0~2 = number of fonts loaded */

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

    /* ================================================================
     * UI 数据绑定 — 每 500ms 刷新一次
     *
     * 数据来源：s_latest_adc（A 板通过 SPI 发来的 ADC 数据）
     *
     * ADC 通道映射（AD7606 8 通道）：
     *   ch[0] = Ua 电压（A相）  ← PT 变比后的模拟信号
     *   ch[1] = Ub 电压（B相）
     *   ch[2] = Uc 电压（C相）
     *   ch[3] = Ia 电流（A相）  ← CT 变比后的模拟信号
     *   ch[4] = Ib 电流（B相）
     *   ch[5] = Ic 电流（C相）
     *   ch[6] = I0 零序电流     ← 三相不平衡电流
     *   ch[7] = 备用
     *
     * STM32 内部 ADC 通道映射（6 通道）：
     *   internal_adc[0] = 温度传感器 1（上触头 A相）
     *   internal_adc[1] = 温度传感器 2（上触头 B相）
     *   internal_adc[2] = 温度传感器 3（上触头 C相）
     *   internal_adc[3] = 温度传感器 4（下触头 A相）
     *   internal_adc[4] = 温度传感器 5（下触头 B相）
     *   internal_adc[5] = 温度传感器 6（下触头 C相）
     *
     * 缩放系数（原始值 → 工程值）：
     *   电压：ch[n] × 0.01 = kV（PT变比已在A板计算）
     *   电流：ch[n] × 0.001 = A（CT变比已在A板计算）
     *   温度：internal_adc[n] × 0.1 = ℃
     * ================================================================ */
    uint32_t now = HAL_GetTick();
    if ((now - ui_update_tick) >= 500) {
      ui_update_tick = now;

      /*
       * [Fix #2] mutex 保护读取 - 拷贝到本地后解锁，LVGL 更新在锁外
       */
      Eric888_ADC_Data local_adc;
      FTS_StatusSnapshot_t local_fts;
      uint8_t adc_ok = 0, fts_ok = 0;

      if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (s_adc_fresh) { local_adc = s_latest_adc; s_adc_fresh = 0; adc_ok = 1; }
        if (s_fts_fresh) { local_fts = s_latest_fts; s_fts_fresh = 0; fts_ok = 1; }
        xSemaphoreGive(s_data_mutex);
      }

      if (adc_ok) {
        char buf[16];

        /* 电压 (kV) */
        snprintf(buf, sizeof(buf), "%.2f", (double)(local_adc.ch[0] * 0.01f));
        lv_label_set_text(ui_Label80, buf);
        snprintf(buf, sizeof(buf), "%.2f", (double)(local_adc.ch[1] * 0.01f));
        lv_label_set_text(ui_Label82, buf);
        snprintf(buf, sizeof(buf), "%.2f", (double)(local_adc.ch[2] * 0.01f));
        lv_label_set_text(ui_Label83, buf);

        /* 电流 (A) */
        snprintf(buf, sizeof(buf), "%.2f", (double)(local_adc.ch[3] * 0.001f));
        lv_label_set_text(ui_Label81, buf);
        snprintf(buf, sizeof(buf), "%.2f", (double)(local_adc.ch[4] * 0.001f));
        lv_label_set_text(ui_Label85, buf);
        snprintf(buf, sizeof(buf), "%.2f", (double)(local_adc.ch[5] * 0.001f));
        lv_label_set_text(ui_Label86, buf);

        /* 上触头温度 */
        snprintf(buf, sizeof(buf), "%.1f\xc2\xb0""C", (double)(local_adc.internal_adc[0] * 0.1f));
        lv_label_set_text(ui_Label190, buf);
        snprintf(buf, sizeof(buf), "%.1f\xc2\xb0""C", (double)(local_adc.internal_adc[1] * 0.1f));
        lv_label_set_text(ui_Label193, buf);
        snprintf(buf, sizeof(buf), "%.1f\xc2\xb0""C", (double)(local_adc.internal_adc[2] * 0.1f));
        lv_label_set_text(ui_Label194, buf);

        /* 下触头温度 */
        snprintf(buf, sizeof(buf), "%.1f\xc2\xb0""C", (double)(local_adc.internal_adc[3] * 0.1f));
        lv_label_set_text(ui_Label200, buf);
        snprintf(buf, sizeof(buf), "%.1f\xc2\xb0""C", (double)(local_adc.internal_adc[4] * 0.1f));
        lv_label_set_text(ui_Label195, buf);
        snprintf(buf, sizeof(buf), "%.1f\xc2\xb0""C", (double)(local_adc.internal_adc[5] * 0.1f));
        lv_label_set_text(ui_Label197, buf);
      }

      /* FTS 保护状态面板 */
      if (fts_ok) {
        ui_fts_update(&local_fts);
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

      /* 请求 FTS 保护状态快照 (背靠背) */
      int ret_fts = BSP_SpiMaster_Transfer(CMD_READ_STATUS, NULL, 0);

      /*
       * [Fix #2] mutex 保护写入 - SPI 传输在锁外完成，仅 memcpy 时持锁
       */
      if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (ret == 0) {
          const Eric888_SPI_Frame *rx = BSP_SpiMaster_GetRxFrame();
          if (rx->cmd == CMD_ACK && rx->len >= sizeof(Eric888_ADC_Data)) {
            memcpy(&s_latest_adc, rx->payload, sizeof(Eric888_ADC_Data));
            s_adc_fresh = 1;
          }
        }
        if (ret_fts == 0) {
          const Eric888_SPI_Frame *rx_fts = BSP_SpiMaster_GetRxFrame();
          if (rx_fts->cmd == CMD_ACK && rx_fts->len >= sizeof(FTS_StatusSnapshot_t)) {
            memcpy(&s_latest_fts, rx_fts->payload, sizeof(FTS_StatusSnapshot_t));
            s_fts_fresh = 1;
          }
        }
        xSemaphoreGive(s_data_mutex);

        /* 同步数据到 Modbus TCP 寄存器表 (供 SCADA 读取) */
        if (s_adc_fresh) {
          ModbusTCP_UpdateADC(s_latest_adc.ch, 6,
                             s_latest_adc.internal_adc, 6);
        }
        if (s_fts_fresh) {
          ModbusTCP_UpdateFTS(s_latest_fts.fts_state, s_latest_fts.fault_type,
                             s_latest_fts.topology, s_latest_fts.backup_available,
                             s_latest_fts.phase_diff_deg10,
                             s_latest_fts.recorder_frozen);
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
          /* Modbus TCP: 接收请求 → 协议解析 → 响应 */
          uint8_t rx_buf[256];
          int rx_len = W5500_Socket_Recv(0, rx_buf, sizeof(rx_buf));
          if (rx_len > 0) {
            uint8_t tx_buf[260];
            int tx_len = 0;
            int ret = ModbusTCP_Process(rx_buf, rx_len, tx_buf, &tx_len);
            if (ret > 0 && tx_len > 0) {
              W5500_Socket_Send(0, tx_buf, (uint16_t)tx_len);
            }
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

/**
 * @brief  [Fix #1] 空闲维护任务
 *
 * 原 BUG: defaultTask 绑定 StartDefaultTask → LVGL 双任务并发 → HardFault
 * 修复: 独立的低优先级维护任务，仅做栈水位监控和空闲计数
 */
void StartIdleMaintenanceTask(void const * argument)
{
  (void)argument;
  for(;;)
  {
    /* 低频空闲循环 — 可用于栈水位监控或系统诊断 */
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
    /* 可选：监控各任务栈水位，低于阈值时亮故障灯 */
    /* UBaseType_t disp_wm = uxTaskGetStackHighWaterMark(displayTaskHandle); */
    /* UBaseType_t comm_wm = uxTaskGetStackHighWaterMark(commTaskHandle); */
#endif
    osDelay(5000);  /* 5 秒一次，几乎不消耗 CPU */
  }
}

/* USER CODE END Application */
