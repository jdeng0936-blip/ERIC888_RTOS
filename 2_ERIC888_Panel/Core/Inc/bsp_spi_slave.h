/**
 * @file    bsp_spi_slave.h
 * @brief   SPI4 Slave communication driver (A-board → B-board)
 * @note    ERIC888 A-board: SPI4 Slave, 8-bit, DMA Normal (v3.0)
 *
 *          Architecture: "Always-Ready" DMA pattern
 *          ─────────────────────────────────────────
 *          1. TX DMA pre-loaded with latest Eric888_SPI_Frame (ADC data)
 *          2. When B-board (Master) clocks, DMA sends automatically
 *          3. On DMA complete: parse RX command → refill TX with fresh data
 *          4. Never miss a Master request — DMA is ALWAYS armed
 *
 *          Data flow:
 *          ISR → DoubleBuffer → TX Mirror → SPI4 DMA → B-board
 *                                ↑ refresh after each transaction
 */
#ifndef BSP_SPI_SLAVE_H
#define BSP_SPI_SLAVE_H

#include "stm32f4xx_hal.h"
#include "eric888_spi_protocol.h"

/**
 * @brief  Initialize SPI4 Slave DMA and pre-load first TX frame
 * @param  hspi: SPI handle (&hspi4)
 * @retval 0=success, -1=fail
 */
int BSP_SpiSlave_Init(SPI_HandleTypeDef *hspi);

/**
 * @brief  Refresh TX mirror buffer with latest ADC data from double-buffer
 *         Called by Task_Comm periodically or after each DMA complete
 * @param  db: pointer to double-buffer (producer=EXTI1 ISR)
 */
void BSP_SpiSlave_RefreshTx(const Eric888_DoubleBuffer *db);

/**
 * @brief  Process received frame from B-board (command parsing)
 *         Called after DMA RX complete
 * @retval parsed command, or CMD_NOP if invalid
 */
Eric888_SPI_Cmd BSP_SpiSlave_ProcessRx(void);

/**
 * @brief  DMA complete callback — called from HAL_SPI_TxRxCpltCallback
 *         Reloads TX with fresh data and re-arms DMA
 * @param  hspi: SPI handle
 */
void BSP_SpiSlave_DmaComplete(SPI_HandleTypeDef *hspi);

/**
 * @brief  Get communication statistics
 */
typedef struct {
  uint32_t tx_count;       /**< Total frames sent */
  uint32_t rx_count;       /**< Total frames received */
  uint32_t crc_errors;     /**< CRC validation failures */
  uint32_t seq_gaps;       /**< Sequence number gaps (lost frames) */
} SpiSlave_Stats;

const SpiSlave_Stats* BSP_SpiSlave_GetStats(void);

#endif /* BSP_SPI_SLAVE_H */
