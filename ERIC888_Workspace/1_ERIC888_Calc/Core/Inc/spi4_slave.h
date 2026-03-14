/**
 * @file   spi4_slave.h
 * @brief  SPI4 Slave 驱动头文件 — A板(Slave) ↔ B板(Master)
 *
 * ✅ [Embedded-Engineer]
 * 快照隔离 + 请求-响应架构：
 *   B板(Master) 发 CMD_READ_STATUS → A板(Slave) 回传保护状态快照
 *   快照构建使用 taskENTER/EXIT_CRITICAL 防止数据撕裂
 *   PI11 握手线通知 B板 "A板就绪"
 */
#ifndef __SPI4_SLAVE_H__
#define __SPI4_SLAVE_H__

#include "stm32f4xx_hal.h"

/* ═══════════ 外部接口 ═══════════ */

/**
 * @brief  初始化 SPI4 Slave 外设 + GPIO + IRQ 握手线
 *         初始化完成后自动预装默认应答并开始监听
 */
void SPI4_Slave_Init(void);

/**
 * @brief  SPI4 全局中断入口
 *         需在 stm32f4xx_it.c 中转发：
 *         void SPI4_IRQHandler(void) { extern void SPI4_IRQHandler(void); }
 */
void SPI4_IRQHandler(void);

#endif /* __SPI4_SLAVE_H__ */
