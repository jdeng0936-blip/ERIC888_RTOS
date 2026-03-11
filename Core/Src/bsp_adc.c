/**
 * @file    bsp_adc.c
 * @brief   Internal ADC1 DMA driver implementation
 *
 *          Usage:
 *          1. Call BSP_ADC_Start() once after MX_ADC1_Init() + MX_TIM3_Init()
 *          2. DMA fills buffer continuously in circular mode
 *          3. Read adc_dma_buf[] at any time for latest values
 */
#include "bsp_adc.h"

int BSP_ADC_Start(ADC_HandleTypeDef *hadc, TIM_HandleTypeDef *htim,
                  uint16_t *buf)
{
    /* Start ADC with DMA in circular mode */
    if (HAL_ADC_Start_DMA(hadc, (uint32_t *)buf, ADC_NUM_CHANNELS) != HAL_OK) {
        return -1;
    }

    /* Start TIM3 to trigger ADC conversions at 25.6 kHz */
    if (HAL_TIM_Base_Start(htim) != HAL_OK) {
        HAL_ADC_Stop_DMA(hadc);
        return -1;
    }

    return 0;
}

void BSP_ADC_Stop(ADC_HandleTypeDef *hadc, TIM_HandleTypeDef *htim)
{
    HAL_TIM_Base_Stop(htim);
    HAL_ADC_Stop_DMA(hadc);
}

uint32_t BSP_ADC_ToMillivolts(uint16_t raw)
{
    /* VREF = 3.3V, 12-bit ADC: mV = raw * 3300 / 4095 */
    return ((uint32_t)raw * 3300) / 4095;
}
