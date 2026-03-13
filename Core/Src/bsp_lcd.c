#include "bsp_lcd.h"
#include "ltdc.h"
#include "dma2d.h"

LTDC_HandleTypeDef hltdc;
DMA2D_HandleTypeDef hdma2d;

void BSP_LCD_Init(void) {
    /* 1. LTDC GPIO & Clock configuration (通常在 ltdc.c 中，由 MX_LTDC_Init 调用) */
    MX_LTDC_Init();
    
    /* 2. 配置层 */
    BSP_LCD_LayerInit(0, LCD_FRAME_BUFFER);
    
    /* 3. 开启显存清零 (使用 DMA2D) */
    MX_DMA2D_Init();
    BSP_DMA2D_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, 0x0000); /* 清黑屏 */
}

void BSP_LCD_LayerInit(uint16_t LayerIndex, uint32_t FB_Addr) {
    LTDC_LayerCfgTypeDef pLayerCfg = {0};

    pLayerCfg.WindowX0 = 0;
    pLayerCfg.WindowX1 = LCD_WIDTH;
    pLayerCfg.WindowY0 = 0;
    pLayerCfg.WindowY1 = LCD_HEIGHT;
    pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
    pLayerCfg.Alpha = 255;
    pLayerCfg.Alpha0 = 0;
    pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
    pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
    pLayerCfg.FBStartAdress = FB_Addr;
    pLayerCfg.ImageWidth = LCD_WIDTH;
    pLayerCfg.ImageHeight = LCD_HEIGHT;
    pLayerCfg.Backcolor.Blue = 0;
    pLayerCfg.Backcolor.Green = 0;
    pLayerCfg.Backcolor.Red = 0;
    
    if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, LayerIndex) != HAL_OK) {
        Error_Handler();
    }
}

void BSP_DMA2D_FillRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    hdma2d.Instance = DMA2D;
    hdma2d.Init.Mode = DMA2D_R2M;
    hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
    hdma2d.Init.OutputOffset = LCD_WIDTH - width;
    
    if (HAL_DMA2D_Init(&hdma2d) == HAL_OK) {
        if (HAL_DMA2D_Start(&hdma2d, color, LCD_FRAME_BUFFER + (y * LCD_WIDTH + x) * 2, width, height) == HAL_OK) {
            HAL_DMA2D_PollForTransfer(&hdma2d, 10);
        }
    }
}
