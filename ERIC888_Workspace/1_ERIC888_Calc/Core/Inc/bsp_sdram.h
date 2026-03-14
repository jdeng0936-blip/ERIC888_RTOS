/**
 * @file   bsp_sdram.h
 * @brief  外部 SDRAM (W9825G6KH / IS42S16160J) 驱动头文件
 *
 * ✅ [Embedded-Engineer]
 * 硬件：STM32F429 FMC Bank2 → 32MB SDRAM (4 Banks × 4M × 16-bit)
 * 映射：0xC0000000 ~ 0xC1FFFFFF
 *
 * 必须在 FreeRTOS 启动前完成初始化和自检！
 */
#ifndef __BSP_SDRAM_H__
#define __BSP_SDRAM_H__

#include "stm32f4xx_hal.h"

/* ═══════════ 地址与容量 ═══════════ */
#define SDRAM_BASE_ADDRESS      0xC0000000UL    /* FMC SDRAM Bank2 起始地址 */
#define SDRAM_SIZE_BYTES        (32UL * 1024 * 1024)  /* 32 MB */

/* ═══════════ 自检魔法数字 ═══════════ */
#define SDRAM_TEST_MAGIC        0x5AA51234UL

/* ═══════════ 外部接口 ═══════════ */

/**
 * @brief  初始化 SDRAM 外设 + FMC GPIO + 上电序列 (严苛时序)
 *         包含：时钟使能 → 100μs延时 → PALL → 8次自刷新 → 模式寄存器加载
 */
void BSP_SDRAM_Init(void);

/**
 * @brief  SDRAM 硬件自检
 *         在起始地址写入魔法数字 0x5AA51234 并回读
 *         不一致则死循环，绝对拦截系统进入 RTOS！
 * @retval 正常不返回失败 (失败时死循环)
 */
void BSP_SDRAM_Test(void);

#endif /* __BSP_SDRAM_H__ */
