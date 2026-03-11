/**
 * @file    bsp_ad7606.c
 * @brief   AD7606 8-channel 16-bit ADC driver implementation
 *
 *          Data acquisition is handled EXCLUSIVELY by EXTI1 ISR (BUSY falling edge):
 *          1. TIM3 CH1 PWM drives CONVST → triggers simultaneous conversion
 *          2. AD7606 asserts BUSY during conversion (~4us)
 *          3. BUSY falls → EXTI1 ISR reads 8 channels via FMC (loop-unrolled)
 *
 *          WARNING: Do NOT add any software-triggered FMC reads here.
 *          Any concurrent FMC access outside the ISR will cause AD7606
 *          internal pointer corruption → channel drift.
 */
#include "bsp_ad7606.h"
#include "main.h"

void BSP_AD7606_Reset(void)
{
    /* RESET pin = PG7, active high */
    /* Pull high for >50ns, then release */
    HAL_GPIO_WritePin(RESET_GPIO_Port, RESET_Pin, GPIO_PIN_SET);
    /* Brief delay: at 180MHz, a few NOPs give >50ns */
    for (volatile int i = 0; i < 20; i++) {}
    HAL_GPIO_WritePin(RESET_GPIO_Port, RESET_Pin, GPIO_PIN_RESET);
    /* Wait for reset to complete (~100ns) */
    for (volatile int i = 0; i < 20; i++) {}
}

int32_t BSP_AD7606_ToMillivolts(int16_t raw, uint8_t range_10v)
{
    /* +/-5V range:  voltage_mV = raw * 5000 / 32768 */
    /* +/-10V range: voltage_mV = raw * 10000 / 32768 */
    if (range_10v) {
        return ((int32_t)raw * 10000) / 32768;
    } else {
        return ((int32_t)raw * 5000) / 32768;
    }
}

