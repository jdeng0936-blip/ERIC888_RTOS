#ifndef __BSP_LCD_H
#define __BSP_LCD_H

#include "main.h"

/* LCD 规格参数 (800x480) */
#define LCD_WIDTH             800
#define LCD_HEIGHT            480

/* LCD 时序参数 (参考典型 7寸 RGB 屏) */
#define LCD_HSYNC             48
#define LCD_HBP               88
#define LCD_HFP               40
#define LCD_VSYNC             3
#define LCD_VBP               32
#define LCD_VFP               13

/* 显存地址 (SDRAM Bank 1) */
#define LCD_FRAME_BUFFER      0xC0000000
#define LCD_BUFFER_SIZE       (LCD_WIDTH * LCD_HEIGHT * 2) /* RGB565 */

void BSP_LCD_Init(void);
void BSP_LCD_LayerInit(uint16_t LayerIndex, uint32_t FB_Addr);
void BSP_DMA2D_FillRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);

#endif
