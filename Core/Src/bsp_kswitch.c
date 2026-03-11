/**
 * @file    bsp_kswitch.c
 * @brief   K-switch GPIO driver implementation
 */
#include "bsp_kswitch.h"
#include "main.h"

/* Pin mapping table: [switch_id][0=ON, 1=OFF] */
static const struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} kswitch_pins[KSWITCH_COUNT][2] = {
    /* K1: ON=PH14, OFF=PH15 */
    [KSWITCH_1] = {
        { GPIOH, GPIO_PIN_14 },  /* ON  */
        { GPIOH, GPIO_PIN_15 },  /* OFF */
    },
    /* K2: ON=PI4, OFF=PH13 */
    [KSWITCH_2] = {
        { GPIOI, GPIO_PIN_4  },  /* ON  */
        { GPIOH, GPIO_PIN_13 },  /* OFF */
    },
    /* K3: ON=PI6, OFF=PI5 */
    [KSWITCH_3] = {
        { GPIOI, GPIO_PIN_6  },  /* ON  */
        { GPIOI, GPIO_PIN_5  },  /* OFF */
    },
    /* K4: ON=PI8, OFF=PI7 */
    [KSWITCH_4] = {
        { GPIOI, GPIO_PIN_8  },  /* ON  */
        { GPIOI, GPIO_PIN_7  },  /* OFF */
    },
};

void BSP_KSwitch_Operate(KSwitch_ID id, KSwitch_Action action, uint32_t pulse_ms)
{
    if (id >= KSWITCH_COUNT) return;

    GPIO_TypeDef *port = kswitch_pins[id][action].port;
    uint16_t pin       = kswitch_pins[id][action].pin;

    /* Drive coil HIGH */
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);

    /* Hold for pulse duration */
    HAL_Delay(pulse_ms);

    /* Release coil */
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
}

void BSP_KSwitch_TripAll(void)
{
    /* Emergency: activate all OFF coils simultaneously */
    for (int i = 0; i < KSWITCH_COUNT; i++) {
        GPIO_TypeDef *port = kswitch_pins[i][KSWITCH_ACTION_OFF].port;
        uint16_t pin       = kswitch_pins[i][KSWITCH_ACTION_OFF].pin;
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    }

    /* Hold all OFF coils for 20ms */
    HAL_Delay(20);

    /* Release all OFF coils */
    for (int i = 0; i < KSWITCH_COUNT; i++) {
        GPIO_TypeDef *port = kswitch_pins[i][KSWITCH_ACTION_OFF].port;
        uint16_t pin       = kswitch_pins[i][KSWITCH_ACTION_OFF].pin;
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
    }
}
