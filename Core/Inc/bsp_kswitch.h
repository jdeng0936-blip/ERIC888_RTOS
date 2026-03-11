/**
 * @file    bsp_kswitch.h
 * @brief   K-switch (fast circuit breaker) GPIO driver
 * @note    ERIC888 A-board - 4 K-switches, each with ON + OFF coils
 *
 *          K1: ON = PH14, OFF = PH15
 *          K2: ON = PI4,  OFF = PH13
 *          K3: ON = PI6,  OFF = PI5
 *          K4: ON = PI8,  OFF = PI7
 *
 *          Drive logic: pulse HIGH to activate coil, then release
 *          Pulse duration: ~10ms typical for relay operation
 */
#ifndef BSP_KSWITCH_H
#define BSP_KSWITCH_H

#include "stm32f4xx_hal.h"

typedef enum {
    KSWITCH_1 = 0,
    KSWITCH_2,
    KSWITCH_3,
    KSWITCH_4,
    KSWITCH_COUNT
} KSwitch_ID;

typedef enum {
    KSWITCH_ACTION_ON = 0,   /* Close the switch */
    KSWITCH_ACTION_OFF       /* Open the switch */
} KSwitch_Action;

/**
 * @brief  Operate a K-switch (pulse the ON or OFF coil)
 * @param  id:     Which switch (KSWITCH_1 ~ KSWITCH_4)
 * @param  action: KSWITCH_ACTION_ON or KSWITCH_ACTION_OFF
 * @param  pulse_ms: Coil drive pulse duration in milliseconds (typical: 10~50)
 */
void BSP_KSwitch_Operate(KSwitch_ID id, KSwitch_Action action, uint32_t pulse_ms);

/**
 * @brief  Emergency trip: immediately close all OFF coils
 *         Used for fault protection - opens all switches
 */
void BSP_KSwitch_TripAll(void);

#endif /* BSP_KSWITCH_H */
