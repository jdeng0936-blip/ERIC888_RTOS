/**
 * @file    bsp_kswitch.c
 * @brief   K-switch GPIO driver implementation (v2.0 — Non-blocking)
 *
 *          v2.0: Replaced blocking HAL_Delay() with FreeRTOS one-shot timers.
 *
 *          Issue with v1.0:
 *            BSP_KSwitch_TripAll() called from CalcTask (osPriorityAboveNormal)
 *            blocked for 20ms via HAL_Delay(), causing DSP batch loss.
 *
 *          Fix:
 *            GPIO coil is driven HIGH immediately (zero delay).
 *            A FreeRTOS one-shot timer fires after pulse_ms to release the coil.
 *            Caller returns instantly — no DSP disruption.
 *
 *          Timer architecture:
 *            - 4 individual timers (one per K-switch) for normal Operate()
 *            - 1 dedicated timer for TripAll() emergency (releases all OFF coils)
 */
#include "bsp_kswitch.h"
#include "main.h"
#include "FreeRTOS.h"
#include "timers.h"

/* ========================= Pin Mapping ========================= */

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

/* ========================= Timer State ========================= */

/**
 * Per-switch timer context: which pin to release when timer fires
 */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} KSwitch_TimerCtx;

static TimerHandle_t s_timers[KSWITCH_COUNT];         /* per-switch timers */
static KSwitch_TimerCtx s_timer_ctx[KSWITCH_COUNT];   /* per-switch release target */

static TimerHandle_t s_trip_timer;                     /* emergency trip timer */

/* ========================= Timer Callbacks ========================= */

/**
 * @brief  Per-switch timer callback: release the coil GPIO
 *         Called from Timer Service Task after pulse_ms expires
 */
static void kswitch_timer_cb(TimerHandle_t xTimer)
{
    /* Timer ID encodes the switch index (0..3) */
    uint32_t idx = (uint32_t)(uintptr_t)pvTimerGetTimerID(xTimer);
    if (idx < KSWITCH_COUNT) {
        HAL_GPIO_WritePin(s_timer_ctx[idx].port, s_timer_ctx[idx].pin,
                          GPIO_PIN_RESET);
    }
}

/**
 * @brief  TripAll timer callback: release ALL OFF coils simultaneously
 */
static void trip_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    for (int i = 0; i < KSWITCH_COUNT; i++) {
        GPIO_TypeDef *port = kswitch_pins[i][KSWITCH_ACTION_OFF].port;
        uint16_t pin       = kswitch_pins[i][KSWITCH_ACTION_OFF].pin;
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
    }
}

/* ========================= Public API ========================= */

void BSP_KSwitch_Init(void)
{
    /* Create per-switch one-shot timers */
    for (int i = 0; i < KSWITCH_COUNT; i++) {
        s_timers[i] = xTimerCreate(
            "KSw",                          /* name (debug only) */
            pdMS_TO_TICKS(20),              /* default period (overridden on use) */
            pdFALSE,                        /* one-shot */
            (void *)(uintptr_t)i,           /* timer ID = switch index */
            kswitch_timer_cb
        );
    }

    /* Create emergency trip timer */
    s_trip_timer = xTimerCreate(
        "Trip",
        pdMS_TO_TICKS(20),                  /* 20ms coil hold */
        pdFALSE,                            /* one-shot */
        NULL,
        trip_timer_cb
    );
}

void BSP_KSwitch_Operate(KSwitch_ID id, KSwitch_Action action, uint32_t pulse_ms)
{
    if (id >= KSWITCH_COUNT) return;
    if (pulse_ms == 0) pulse_ms = 1;  /* minimum 1ms */

    GPIO_TypeDef *port = kswitch_pins[id][action].port;
    uint16_t pin       = kswitch_pins[id][action].pin;

    /* Save release target for timer callback */
    s_timer_ctx[id].port = port;
    s_timer_ctx[id].pin  = pin;

    /* Drive coil HIGH immediately (zero delay) */
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);

    /* Start one-shot timer to release coil after pulse_ms */
    if (s_timers[id] != NULL) {
        /* If timer is already running (previous pulse not finished),
         * xTimerChangePeriod will restart it with new period */
        xTimerChangePeriod(s_timers[id], pdMS_TO_TICKS(pulse_ms), 0);
    }
}

void BSP_KSwitch_TripAll(void)
{
    /* Emergency: activate all OFF coils simultaneously — ZERO DELAY */
    for (int i = 0; i < KSWITCH_COUNT; i++) {
        GPIO_TypeDef *port = kswitch_pins[i][KSWITCH_ACTION_OFF].port;
        uint16_t pin       = kswitch_pins[i][KSWITCH_ACTION_OFF].pin;
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    }

    /* Start one-shot timer to release all OFF coils after 20ms */
    if (s_trip_timer != NULL) {
        xTimerChangePeriod(s_trip_timer, pdMS_TO_TICKS(20), 0);
    }
}
