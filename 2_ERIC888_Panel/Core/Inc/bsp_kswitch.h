/**
 * @file    bsp_kswitch.h
 * @brief   K-switch (fast circuit breaker) GPIO driver — Non-blocking v2.0
 * @note    ERIC888 A-board - 4 K-switches, each with ON + OFF coils
 *
 *          K1: ON = PH14, OFF = PH15
 *          K2: ON = PI4,  OFF = PH13
 *          K3: ON = PI6,  OFF = PI5
 *          K4: ON = PI8,  OFF = PI7
 *
 *          Drive logic: pulse HIGH to activate coil, then release
 *          Pulse duration: ~10–50ms typical for relay operation
 *
 *  v2.0 changes:
 *    - Replaced blocking HAL_Delay() with FreeRTOS one-shot timers
 *    - BSP_KSwitch_Init() MUST be called after RTOS scheduler starts
 *    - All operations are now non-blocking (return immediately)
 *    - Timer callback releases coil GPIO asynchronously
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
 * @brief  Initialize K-switch driver (create FreeRTOS timers)
 *         MUST be called AFTER osKernelStart() (from a task context)
 */
void BSP_KSwitch_Init(void);

/**
 * @brief  Operate a K-switch (pulse the ON or OFF coil) — NON-BLOCKING
 *         Drives coil HIGH immediately, starts a one-shot timer to release.
 * @param  id:       Which switch (KSWITCH_1 ~ KSWITCH_4)
 * @param  action:   KSWITCH_ACTION_ON or KSWITCH_ACTION_OFF
 * @param  pulse_ms: Coil drive pulse duration in milliseconds (typical: 10~50)
 */
void BSP_KSwitch_Operate(KSwitch_ID id, KSwitch_Action action, uint32_t pulse_ms);

/**
 * @brief  Emergency trip: immediately drive all OFF coils HIGH — NON-BLOCKING
 *         Coils are released asynchronously after 20ms by one-shot timer.
 *         Safe to call from any task context (does NOT block the caller).
 */
void BSP_KSwitch_TripAll(void);

#endif /* BSP_KSWITCH_H */
