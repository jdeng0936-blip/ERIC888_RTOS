/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
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
#include "fmc.h"
#include "gpio.h"


/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "task.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// ERIC888 快切点火核心：AD7606 挂载在 FMC NE4，基地址直接映射
#define AD7606_BASE_ADDR ((uint32_t)0xD0000000)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
// 定义一个全局数组，用来存储 8 个通道的电压/电流实时数据
int16_t ad7606_data[8];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick.
   */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_FMC_Init();
  
  /*
   * ✅ [Embedded-Engineer] SDRAM 初始化 + 硬件自检
   *
   * ⚠️ 必须在所有使用 SDRAM 的模块 (如 FaultRecorder) 之前调用！
   * 自检失败时死循环，绝对拦截 RTOS 启动，防止 HardFault。
   */
  extern void BSP_SDRAM_Init(void);
  extern void BSP_SDRAM_Test(void);
  BSP_SDRAM_Init();
  BSP_SDRAM_Test();
  
  /* ✅ [Embedded-Engineer] 初始化 AD7606 的 TIM3 硬件触发信号 */
  extern void MX_TIM3_Init_AD7606_Trigger(void);
  MX_TIM3_Init_AD7606_Trigger();
  
  /* ✅ [Embedded-Engineer] 初始化内部 ADC1 + DMA 6路辅助电流采集 */
  extern void MX_ADC1_Current_Init(void);
  MX_ADC1_Current_Init();
  
  /* USER CODE BEGIN 2 */
  // AD7606 硬件复位 (PG7 拉低再拉高)
  HAL_GPIO_WritePin(GPIOG, GPIO_PIN_7, GPIO_PIN_RESET);
  for (volatile int i = 0; i < 100; i++);
  HAL_GPIO_WritePin(GPIOG, GPIO_PIN_7, GPIO_PIN_SET);
  
  /* ✅ [Embedded-Engineer] 初始化 DWT 周期计数器 (纳秒级性能测量) */
  extern void DWT_Init(void);
  DWT_Init();
  
  /* ✅ [Embedded-Engineer] 创建 FTS 保护任务 + K-Switch GPIO 初始化 */
  extern void FTS_Protect_Init(void);
  FTS_Protect_Init();
  
  /* ✅ [Embedded-Engineer] 初始化 SPI4 Slave (A→B板通信) */
  extern void SPI4_Slave_Init(void);
  SPI4_Slave_Init();
  
  /* ✅ [Embedded-Engineer] 启动 FreeRTOS 调度器 —— 一旦启动将不会返回 */
  vTaskStartScheduler();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  /* 如果执行到这里，说明 FreeRTOS 堆不够大，调度器启动失败 */
  while (1) {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_7);
    HAL_Delay(100); /* 快速闪烁 = 错误指示 */
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
   */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   * ✅ [HSE 8MHz] 极速快切点火：使用外部 8MHz 晶振以保证 1ms 算法的极高时钟精度
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;         // 8MHz / 8 = 1MHz
  RCC_OscInitStruct.PLL.PLLN = 360;       // 1MHz * 360 = 360MHz
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2; // 360MHz / 2 = 180MHz (内核时钟)
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
   */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1) {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 * where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line) {
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line
     number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
     line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/* ═══════════════════ FreeRTOS 必需回调函数 ═══════════════════ */

/*
 * ✅ [Embedded-Engineer]
 * configSUPPORT_STATIC_ALLOCATION = 1 时必须提供这两个回调。
 * FreeRTOS 内核需要它们来获取 Idle 和 Timer 任务的静态内存。
 */

/* Idle 任务的静态内存 */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer   = &xIdleTaskTCBBuffer;
    *ppxIdleTaskStackBuffer = &xIdleStack[0];
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

/* Timer 任务的静态内存 (如果 configUSE_TIMERS = 1) */
#if (configUSE_TIMERS == 1)
static StaticTask_t xTimerTaskTCBBuffer;
static StackType_t xTimerStack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer   = &xTimerTaskTCBBuffer;
    *ppxTimerTaskStackBuffer = &xTimerStack[0];
    *pulTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
}
#endif

/* 栈溢出检测钩子 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    /* 到这里说明某个任务栈溢出了，死循环便于调试 */
    __disable_irq();
    while(1);
}