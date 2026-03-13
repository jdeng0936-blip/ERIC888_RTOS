#include "ltdc.h"

void MX_LTDC_Init(void) {
    hltdc.Instance = LTDC;
    hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
    hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
    hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
    hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
    
    hltdc.Init.HorizontalSync = LCD_HSYNC - 1;
    hltdc.Init.VerticalSync = LCD_VSYNC - 1;
    hltdc.Init.AccumulatedHBP = LCD_HSYNC + LCD_HBP - 1;
    hltdc.Init.AccumulatedVBP = LCD_VSYNC + LCD_VBP - 1;
    hltdc.Init.AccumulatedActiveW = LCD_HSYNC + LCD_HBP + LCD_WIDTH - 1;
    hltdc.Init.AccumulatedActiveH = LCD_VSYNC + LCD_VBP + LCD_HEIGHT - 1;
    hltdc.Init.TotalWidth = LCD_HSYNC + LCD_HBP + LCD_WIDTH + LCD_HFP - 1;
    hltdc.Init.TotalHeigh = LCD_VSYNC + LCD_VBP + LCD_HEIGHT + LCD_VFP - 1;
    
    hltdc.Init.Backcolor.Blue = 0;
    hltdc.Init.Backcolor.Green = 0;
    hltdc.Init.Backcolor.Red = 0;
    
    if (HAL_LTDC_Init(&hltdc) != HAL_OK) {
        Error_Handler();
    }
}

void HAL_LTDC_MspInit(LTDC_HandleTypeDef* ltdcHandle) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if(ltdcHandle->Instance==LTDC) {
        __HAL_RCC_LTDC_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        __HAL_RCC_GPIOF_CLK_ENABLE();
        __HAL_RCC_GPIOG_CLK_ENABLE();

        /**LTDC GPIO Configuration
        PA3     ------> LTDC_B5
        PA4     ------> LTDC_VSYNC
        PA6     ------> LTDC_G2
        PB0     ------> LTDC_R3
        PB1     ------> LTDC_R6
        PB8     ------> LTDC_B6
        PB9     ------> LTDC_B7
        PC6     ------> LTDC_HSYNC
        PC7     ------> LTDC_G6
        PC10    ------> LTDC_R2
        PD3     ------> LTDC_G7
        PD6     ------> LTDC_B2
        PF10    ------> LTDC_DE
        PG6     ------> LTDC_R7
        PG7     ------> LTDC_D7 (Note: Check if this is G3/R7, assuming standard map)
        PG10    ------> LTDC_G3
        PG11    ------> LTDC_B3
        PG12    ------> LTDC_B4
        */
        // Note: Pin mapping below is a generic F429 800x480 RGB565 map. 
        // Real hardware pins must be verified against 888B schematic.
        
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
        
        // PA3, PA4, PA6
        GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_6;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
        
        // PB0, PB1, PB8, PB9
        GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_8|GPIO_PIN_9;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
        
        // PC6, PC7, PC10
        GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_10;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
        
        // PD3, PD6
        GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_6;
        HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
        
        // PF10
        GPIO_InitStruct.Pin = GPIO_PIN_10;
        HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);
        
        // PG6, PG7, PG10, PG11, PG12
        GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12;
        HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
    }
}
