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
#define TRIG_ON_4_Pin GPIO_PIN_8
#define TRIG_ON_4_GPIO_Port GPIOI
#define AD7606_BUSY_Pin GPIO_PIN_1
#define AD7606_BUSY_GPIO_Port GPIOB
#define AD7606_BUSY_EXTI_IRQn EXTI1_IRQn
#define RESET_Pin GPIO_PIN_7
#define RESET_GPIO_Port GPIOG
#define CONVST_Pin GPIO_PIN_6
#define CONVST_GPIO_Port GPIOC
#define TRIG_OFF_2_Pin GPIO_PIN_13
#define TRIG_OFF_2_GPIO_Port GPIOH
#define TRIG_ON_1_Pin GPIO_PIN_14
#define TRIG_ON_1_GPIO_Port GPIOH
#define TRIG_OFF_1_Pin GPIO_PIN_15
#define TRIG_OFF_1_GPIO_Port GPIOH
#define TRIG_ON_1I4_Pin GPIO_PIN_4
#define TRIG_ON_1I4_GPIO_Port GPIOI
#define TRIG_OFF_3_Pin GPIO_PIN_5
#define TRIG_OFF_3_GPIO_Port GPIOI
#define TRIG_ON_3_Pin GPIO_PIN_6
#define TRIG_ON_3_GPIO_Port GPIOI
#define TRIG_OFF_4_Pin GPIO_PIN_7
#define TRIG_OFF_4_GPIO_Port GPIOI

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
