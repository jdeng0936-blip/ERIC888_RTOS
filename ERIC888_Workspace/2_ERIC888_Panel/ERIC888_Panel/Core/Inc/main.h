/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define JDQ0_Pin GPIO_PIN_3
#define JDQ0_GPIO_Port GPIOE
#define KEY_S2_Pin GPIO_PIN_8
#define KEY_S2_GPIO_Port GPIOF
#define KEY_S3_Pin GPIO_PIN_9
#define KEY_S3_GPIO_Port GPIOF
#define KEY_S4_Pin GPIO_PIN_10
#define KEY_S4_GPIO_Port GPIOF
#define W5500_INT_Pin GPIO_PIN_0
#define W5500_INT_GPIO_Port GPIOC
#define W5500_RST_Pin GPIO_PIN_1
#define W5500_RST_GPIO_Port GPIOC
#define TOUCH_RST_Pin GPIO_PIN_7
#define TOUCH_RST_GPIO_Port GPIOA
#define TOUCH_INT_Pin GPIO_PIN_7
#define TOUCH_INT_GPIO_Port GPIOH
#define LED_OPEN_Pin GPIO_PIN_9
#define LED_OPEN_GPIO_Port GPIOH
#define LED_RUN_Pin GPIO_PIN_10
#define LED_RUN_GPIO_Port GPIOH
#define LED_FAULT_Pin GPIO_PIN_11
#define LED_FAULT_GPIO_Port GPIOH
#define LED_CLOSE_Pin GPIO_PIN_12
#define LED_CLOSE_GPIO_Port GPIOH
#define DTR_4G_Pin GPIO_PIN_8
#define DTR_4G_GPIO_Port GPIOA
#define RESET_4G_Pin GPIO_PIN_12
#define RESET_4G_GPIO_Port GPIOA
#define POWER_4G_Pin GPIO_PIN_3
#define POWER_4G_GPIO_Port GPIOD
#define RS485_TXEN_Pin GPIO_PIN_8
#define RS485_TXEN_GPIO_Port GPIOB
#define JDQ4_Pin GPIO_PIN_4
#define JDQ4_GPIO_Port GPIOI
#define JDQ3_Pin GPIO_PIN_5
#define JDQ3_GPIO_Port GPIOI
#define JDQ2_Pin GPIO_PIN_6
#define JDQ2_GPIO_Port GPIOI
#define JDQ1_Pin GPIO_PIN_7
#define JDQ1_GPIO_Port GPIOI

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
