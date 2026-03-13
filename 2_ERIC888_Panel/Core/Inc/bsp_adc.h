/**
 * @file    bsp_adc.h
 * @brief   Internal ADC1 DMA driver (6 channels, TIM3 triggered)
 * @note    ERIC888 A-board current sensing
 *
 *          Channel mapping (CubeMX configured):
 *          Rank1: IN1  (PA1) - Current sense 1
 *          Rank2: IN2  (PA2) - Current sense 2
 *          Rank3: IN10 (PC0) - Current sense 3
 *          Rank4: IN11 (PC1) - Current sense 4
 *          Rank5: IN12 (PC2) - Current sense 5
 *          Rank6: IN13 (PC3) - Current sense 6
 *
 *          Trigger: TIM3 TRGO at 25.6 kHz
 *          DMA: Circular mode, auto-fills buffer
 */
#ifndef BSP_ADC_H
#define BSP_ADC_H

#include "stm32f4xx_hal.h"

#define ADC_NUM_CHANNELS    6

/**
 * @brief  Start ADC1 DMA continuous acquisition (TIM3 triggered)
 * @param  hadc:  ADC handle (from CubeMX: &hadc1)
 * @param  htim:  Timer handle (from CubeMX: &htim3)
 * @param  buf:   DMA destination buffer, must be uint16_t[ADC_NUM_CHANNELS]
 * @retval 0=success, -1=fail
 */
int BSP_ADC_Start(ADC_HandleTypeDef *hadc, TIM_HandleTypeDef *htim,
                  uint16_t *buf);

/**
 * @brief  Stop ADC1 DMA acquisition
 * @param  hadc:  ADC handle
 * @param  htim:  Timer handle
 */
void BSP_ADC_Stop(ADC_HandleTypeDef *hadc, TIM_HandleTypeDef *htim);

/**
 * @brief  Convert raw 12-bit ADC to millivolts (3.3V reference)
 * @param  raw: 12-bit ADC reading (0~4095)
 * @retval voltage in millivolts (0~3300)
 */
uint32_t BSP_ADC_ToMillivolts(uint16_t raw);

#endif /* BSP_ADC_H */
