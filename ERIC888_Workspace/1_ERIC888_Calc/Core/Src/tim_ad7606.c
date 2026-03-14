#include "main.h"
// Force recompile

/*
 * ✅ [Embedded-Engineer] 
 * 硬件化 25.6kHz 采样发令枪
 * 抛弃原来的软件延时死等，改用 TIM3_CH1 (PC6) 硬件输出 PWM
 * 频率: 180MHz (APB1 Timer Clock) / (89+1) / (77+1) ≈ 25641Hz ≈ 25.6kHz
 * 这样 MCU 完全不需要管发号指令，PWM 自动等间距敲击 CONVST。
 */
void MX_TIM3_Init_AD7606_Trigger(void)
{
    __HAL_RCC_TIM3_CLK_ENABLE();
    
    // PC6 作为 TIM3_CH1 复用功能
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP; // 复用推挽
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    TIM_HandleTypeDef htim3 = {0};
    htim3.Instance = TIM3;
    // APB1 定时器时钟 = 180MHz (180MHz / 90 / 78 = 25641 Hz)
    htim3.Init.Prescaler = 90 - 1; 
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 78 - 1; 
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&htim3);

    TIM_OC_InitTypeDef sConfigOC = {0};
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 39; // 50% 占空比脉冲 (39/78)
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1);
    
    // 启动硬件 PWM 发令
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
}
