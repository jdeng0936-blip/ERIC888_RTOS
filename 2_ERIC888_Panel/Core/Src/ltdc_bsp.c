#include "main.h"

LTDC_HandleTypeDef hltdc;

/**
  * @brief LTDC Initialization Function
  * @param None
  * @retval None
  */
void MX_LTDC_Init(void)
{
  LTDC_LayerCfgTypeDef pLayerCfg = {0};

  hltdc.Instance = LTDC;
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  
  /* 800x480 Generic Panel Timings */
  hltdc.Init.HorizontalSync = 47;
  hltdc.Init.VerticalSync = 2;
  hltdc.Init.AccumulatedHBP = 87;
  hltdc.Init.AccumulatedVBP = 34;
  hltdc.Init.AccumulatedActiveW = 887;
  hltdc.Init.AccumulatedActiveH = 514;
  hltdc.Init.TotalWidth = 927;
  hltdc.Init.TotalHeigh = 527;
  
  hltdc.Init.Backcolor.Blue = 0;
  hltdc.Init.Backcolor.Green = 0;
  hltdc.Init.Backcolor.Red = 0;
  
  if (HAL_LTDC_Init(&hltdc) != HAL_OK)
  {
    Error_Handler();
  }

  pLayerCfg.WindowX0 = 0;
  pLayerCfg.WindowX1 = 800;
  pLayerCfg.WindowY0 = 0;
  pLayerCfg.WindowY1 = 480;
  pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  pLayerCfg.Alpha = 255;
  pLayerCfg.Alpha0 = 0;
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
  /* FrameBuffer in SDRAM Zone 0xC0000000 */
  pLayerCfg.FBStartAdress = 0xC0000000;
  pLayerCfg.ImageWidth = 800;
  pLayerCfg.ImageHeight = 480;
  pLayerCfg.Backcolor.Blue = 0;
  pLayerCfg.Backcolor.Green = 0;
  pLayerCfg.Backcolor.Red = 0;
  
  if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 1) != HAL_OK)
  {
    Error_Handler();
  }
}
