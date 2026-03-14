/**
 * @file lv_port_disp.c
 * LVGL display driver for ERIC888 B-board
 *
 * Uses LTDC Layer0 framebuffer at 0xD0000000 (SDRAM)
 * Double-buffering with partial update for best performance
 */
#include "lv_port_disp.h"
#include "stm32f4xx_hal.h"
#include "ltdc.h"

/* Framebuffer in SDRAM (800 x 480 x 2 bytes = 768,000 bytes each) */
#define FB_ADDR_0  ((uint32_t)0xD0000000)
#define FB_ADDR_1  ((uint32_t)0xD0000000 + 800 * 480 * 2)

/* LVGL draw buffer in SDRAM (after framebuffers + LVGL heap) */
/* FB0: 0xD0000000 (768KB), FB1: skip, LVGL heap: 0xD0200000 (128KB) */
/* Draw buffer start: 0xD0220000 */
#define LV_BUF_ADDR ((uint32_t)0xD0220000)
static lv_disp_draw_buf_t draw_buf;
/* Use SDRAM for draw buffer — 800 x 10 lines = 16,000 bytes */

static lv_disp_drv_t  disp_drv;

/**
 * Flush callback: copy draw buffer to LTDC framebuffer via DMA2D
 */
static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;

    /* Destination in framebuffer */
    uint32_t dst_addr = FB_ADDR_0 + 2 * (area->y1 * 800 + area->x1);

    /* Use DMA2D for fast memory-to-memory copy with pixel format conversion */
    DMA2D->CR = 0;                              /* Memory-to-memory (no PFC) */
    DMA2D->FGMAR = (uint32_t)color_p;           /* Source address */
    DMA2D->OMAR = dst_addr;                     /* Dest address */
    DMA2D->FGOR = 0;                            /* Source line offset = 0 (contiguous) */
    DMA2D->OOR = 800 - w;                       /* Dest line offset */
    DMA2D->FGPFCCR = DMA2D_OUTPUT_RGB565;       /* Pixel format */
    DMA2D->OPFCCR = DMA2D_OUTPUT_RGB565;
    DMA2D->NLR = (uint32_t)(w << 16) | (uint16_t)h;  /* Width | Height */

    /* Start transfer */
    DMA2D->CR |= DMA2D_CR_START;

    /* Wait for completion */
    while (DMA2D->CR & DMA2D_CR_START) { }

    /* Notify LVGL that flushing is done */
    lv_disp_flush_ready(drv);
}

/**
 * Initialize the LVGL display driver
 */
void lv_port_disp_init(void)
{
    /* Clear framebuffer to black */
    uint16_t *fb = (uint16_t *)FB_ADDR_0;
    for (uint32_t i = 0; i < 800 * 480; i++) {
        fb[i] = 0x0000;
    }

    /* Initialize draw buffer (in SDRAM) */
    lv_color_t *buf1 = (lv_color_t *)LV_BUF_ADDR;
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, 800 * 10);

    /* Initialize display driver */
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 800;
    disp_drv.ver_res = 480;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 0;  /* Partial update mode */

    lv_disp_drv_register(&disp_drv);
}
