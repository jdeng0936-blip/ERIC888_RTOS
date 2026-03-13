/**
 * @file    bsp_ad7606.h
 * @brief   AD7606 8-channel 16-bit ADC driver (FMC NE4 parallel interface)
 * @note    ERIC888 A-board voltage/current acquisition
 *
 *          Pin assignment:
 *          - Data bus: FMC D0~D15 (parallel read via NE4)
 *          - CONVST:   PC6 (TIM3 CH1 PWM → hardware trigger)
 *          - BUSY:     PB1 (EXTI1 falling edge → ISR reads all 8 channels)
 *          - RESET:    PG7 (hardware reset, active high)
 *          - OS[2:0]:  tied low (no oversampling)
 *
 *  WARNING: FMC reads to AD7606 must ONLY occur inside EXTI1 ISR.
 *           Any concurrent FMC access will corrupt the AD7606 channel pointer.
 */
#ifndef BSP_AD7606_H
#define BSP_AD7606_H

#include "stm32f4xx_hal.h"

/* FMC NE4 base address for AD7606 */
#define AD7606_BASE_ADDR      ((uint32_t)0x6C000000)

/* Number of channels */
#define AD7606_CHANNELS       8

/* AD7606 voltage range: bipolar +/-5V or +/-10V (depends on RANGE pin) */
/* With RANGE pin LOW:  +/-5V  -> LSB = 5V/32768 = 152.588uV */
/* With RANGE pin HIGH: +/-10V -> LSB = 10V/32768 = 305.176uV */

/**
 * @brief  AD7606 hardware reset
 */
void BSP_AD7606_Reset(void);

/**
 * @brief  Convert raw AD7606 reading to voltage (millivolts)
 * @param  raw: raw 16-bit signed reading
 * @param  range_10v: 1 = +/-10V range, 0 = +/-5V range
 * @retval voltage in millivolts
 */
int32_t BSP_AD7606_ToMillivolts(int16_t raw, uint8_t range_10v);

#endif /* BSP_AD7606_H */

