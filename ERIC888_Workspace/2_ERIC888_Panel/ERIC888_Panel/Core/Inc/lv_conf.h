/**
 * @file lv_conf.h
 * LVGL v8.3 configuration for ERIC888 B-board
 * 800x480 RGB565 LTDC display + STMPE811 touch
 * SquareLine Studio UI integrated
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH     16      /* RGB565 */
#define LV_COLOR_16_SWAP   0       /* No swap for LTDC parallel */
#define LV_COLOR_SCREEN_TRANSP 0
#define LV_COLOR_MIX_ROUND_OFS 128
#define LV_COLOR_CHROMA_KEY lv_color_hex(0x00ff00)

/*====================
   MEMORY SETTINGS
 *====================*/
/* LVGL heap in SDRAM for large SquareLine UI */
#define LV_MEM_CUSTOM      0
#define LV_MEM_SIZE         (128U * 1024U)  /* 128KB — needed for complex SLS UI */
#define LV_MEM_ADR          0xD0200000      /* SDRAM offset (after 2x framebuffer) */
#define LV_MEM_BUF_MAX_NUM  16

/*====================
   HAL SETTINGS
 *====================*/
#define LV_TICK_CUSTOM       1
#define LV_TICK_CUSTOM_INCLUDE "stm32f4xx_hal.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (HAL_GetTick())

/*====================
   DISPLAY SETTINGS
 *====================*/
#define LV_HOR_RES_MAX      800
#define LV_VER_RES_MAX      480
#define LV_DPI_DEF           130    /* 7" 800x480 ~ 130 DPI */
#define LV_DISP_DEF_REFR_PERIOD  16  /* ~60 FPS */

/* DMA2D acceleration for STM32 */
#define LV_USE_GPU_STM32_DMA2D  1
#define LV_GPU_DMA2D_CMSIS_INCLUDE "stm32f4xx.h"

/*====================
   FILE SYSTEM (for SquareLine image loading)
 *====================*/
#define LV_USE_FS_FATFS    0       /* We'll use LVGL's POSIX-like FS stub */
#define LV_USE_FS_STDIO    0
#define LV_USE_FS_POSIX    0
#define LV_USE_FS_WIN32    0
#define LV_USE_FS_LITTLEFS 0

/*====================
   LOGGING
 *====================*/
#define LV_USE_LOG       0

/*====================
   ASSERTS
 *====================*/
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0
#define LV_ASSERT_HANDLER_INCLUDE   <stdint.h>
#define LV_ASSERT_HANDLER while(1);

/*====================
   FONT SETTINGS
 *====================*/
/* Built-in Montserrat fonts - only need 14 as fallback default */
#define LV_FONT_MONTSERRAT_8     0
#define LV_FONT_MONTSERRAT_10    0
#define LV_FONT_MONTSERRAT_12    0
#define LV_FONT_MONTSERRAT_14    1   /* Default fallback */
#define LV_FONT_MONTSERRAT_16    1
#define LV_FONT_MONTSERRAT_18    1
#define LV_FONT_MONTSERRAT_20    1
#define LV_FONT_MONTSERRAT_22    0
#define LV_FONT_MONTSERRAT_24    0
#define LV_FONT_MONTSERRAT_26    0
#define LV_FONT_MONTSERRAT_28    0
#define LV_FONT_MONTSERRAT_30    0
#define LV_FONT_MONTSERRAT_32    0
#define LV_FONT_MONTSERRAT_34    0
#define LV_FONT_MONTSERRAT_36    0
#define LV_FONT_MONTSERRAT_38    0
#define LV_FONT_MONTSERRAT_40    0
#define LV_FONT_MONTSERRAT_42    0
#define LV_FONT_MONTSERRAT_44    0
#define LV_FONT_MONTSERRAT_46    0
#define LV_FONT_MONTSERRAT_48    0

#define LV_FONT_MONTSERRAT_12_SUBPX  0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK       0
#define LV_FONT_UNSCII_8     0
#define LV_FONT_UNSCII_16    0
#define LV_FONT_FMT_TXT_LARGE  1   /* SquareLine custom fonts may be large */
#define LV_USE_FONT_COMPRESSED 1   /* FastLZ compressed images */
#define LV_USE_FONT_SUBPX     0
#if LV_USE_FONT_SUBPX
#define LV_FONT_SUBPX_BGR  0
#endif
/* SquareLine uses custom Chinese fonts as default */
#define LV_FONT_CUSTOM_DECLARE  LV_FONT_DECLARE(ui_font_pfht14);
#define LV_FONT_DEFAULT    &lv_font_montserrat_14

/*====================
   THEME SETTINGS
 *====================*/
#define LV_USE_THEME_DEFAULT   1
#if LV_USE_THEME_DEFAULT
#define LV_THEME_DEFAULT_DARK   1   /* Dark theme for industrial panel */
#define LV_THEME_DEFAULT_GROW   1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif
#define LV_USE_THEME_BASIC     1
#define LV_USE_THEME_MONO      0

/*====================
   WIDGET SETTINGS
 *====================*/
#define LV_USE_ARC         1
#define LV_USE_BAR         1
#define LV_USE_BTN         1
#define LV_USE_BTNMATRIX   1
#define LV_USE_CANVAS      0
#define LV_USE_CHECKBOX    1
#define LV_USE_DROPDOWN    1
#define LV_USE_IMG         1
#define LV_USE_LABEL       1
#define LV_USE_LINE        1
#define LV_USE_ROLLER      1
#define LV_USE_SLIDER      1
#define LV_USE_SWITCH      1
#define LV_USE_TEXTAREA    1
#define LV_USE_TABLE       1

/*====================
   EXTRA WIDGETS
 *====================*/
#define LV_USE_ANIMIMG     0
#define LV_USE_CALENDAR    0
#define LV_USE_CHART       1   /* For waveform display */
#define LV_USE_COLORWHEEL  0
#define LV_USE_IMGBTN      0
#define LV_USE_KEYBOARD    1   /* SquareLine UI uses keyboards */
#define LV_USE_LED         1
#define LV_USE_LIST        1
#define LV_USE_MENU        1
#define LV_USE_METER       1
#define LV_USE_MSGBOX      1
#define LV_USE_SPAN        1
#define LV_USE_SPINBOX     1
#define LV_USE_SPINNER     1
#define LV_USE_TABVIEW     1   /* Main navigation */
#define LV_USE_TILEVIEW    0
#define LV_USE_WIN         0

/*====================
   LAYOUTS
 *====================*/
#define LV_USE_FLEX        1
#define LV_USE_GRID        1

/*====================
   OTHER
 *====================*/
#define LV_USE_SNAPSHOT    0
#define LV_BUILD_EXAMPLES  0

#endif /*LV_CONF_H*/
