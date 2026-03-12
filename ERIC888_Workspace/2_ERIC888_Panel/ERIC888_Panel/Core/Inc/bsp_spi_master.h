/**
 * @file    bsp_spi_master.h
 * @brief   SPI4 Master communication driver (B-board ← A-board)
 * @note    ERIC888 B-board: SPI4 Master, 8-bit, DMA Normal
 *
 *          Architecture: "Request-Response" with PI11 IRQ handshake
 *          ─────────────────────────────────────────────────────────
 *          1. A-board asserts PI11 when batch buffer (512 samples) is ready
 *          2. B-board EXTI11 ISR sets flag → wakes CommTask
 *          3. CommTask pulls NSS low, starts SPI DMA full-duplex transfer
 *          4. On DMA complete: NSS high, validate CRC, process data
 *
 *          Data flow:
 *          A-board PI11 → EXTI11 ISR → CommTask → SPI4 DMA → RX parse
 */
#ifndef BSP_SPI_MASTER_H
#define BSP_SPI_MASTER_H

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "eric888_spi_protocol.h"

/* ========================= Configuration ========================= */

/** PE4 = Software NSS (Master output) */
#define SPI_NSS_PORT   GPIOE
#define SPI_NSS_PIN    GPIO_PIN_4

/** PI11 = Batch ready IRQ from A-board (input, rising edge) */
#define SPI_IRQ_PORT   GPIOI
#define SPI_IRQ_PIN    GPIO_PIN_11

/* ========================= Status ========================= */

typedef struct {
  uint32_t tx_count;        /**< Total frames sent */
  uint32_t rx_count;        /**< Total valid frames received */
  uint32_t crc_errors;      /**< CRC validation failures */
  uint32_t header_errors;   /**< Bad header count */
  uint32_t seq_gaps;        /**< Sequence number gaps */
  uint8_t  last_seq;        /**< Last received sequence number */
  uint8_t  batch_pending;   /**< 1 = PI11 fired, batch ready to read */
} SpiMaster_Stats;

/* ========================= API ========================= */

/**
 * @brief  Initialize SPI4 Master + PI11 EXTI + software NSS
 * @param  hspi: SPI handle (&hspi4)
 * @retval 0=success, -1=fail
 */
int BSP_SpiMaster_Init(SPI_HandleTypeDef *hspi);

/**
 * @brief  Send a request frame and receive response via DMA
 *         Blocks until DMA complete (timeout via FreeRTOS)
 * @param  cmd: Command to send
 * @param  payload: Optional payload data (NULL if none)
 * @param  payload_len: Payload length (0 if none)
 * @retval 0=success, -1=CRC error, -2=header error, -3=timeout
 */
int BSP_SpiMaster_Transfer(Eric888_SPI_Cmd cmd,
                            const uint8_t *payload,
                            uint8_t payload_len);

/**
 * @brief  Get pointer to last received frame (valid after successful transfer)
 */
const Eric888_SPI_Frame* BSP_SpiMaster_GetRxFrame(void);

/**
 * @brief  Check if A-board batch is ready (PI11 IRQ fired)
 * @retval 1=batch ready, 0=not ready
 */
uint8_t BSP_SpiMaster_IsBatchReady(void);

/**
 * @brief  Bind CommTask for instant notification from EXTI ISR
 */
void    BSP_SpiMaster_SetCommTask(TaskHandle_t hTask);

/**
 * @brief  Clear batch ready flag (after reading)
 */
void BSP_SpiMaster_ClearBatchReady(void);

/**
 * @brief  DMA complete callback — called from HAL_SPI_TxRxCpltCallback
 */
void BSP_SpiMaster_DmaComplete(SPI_HandleTypeDef *hspi);

/**
 * @brief  PI11 EXTI callback — called from HAL_GPIO_EXTI_Callback
 */
void BSP_SpiMaster_EXTI_Callback(uint16_t GPIO_Pin);

/**
 * @brief  Get communication statistics
 */
const SpiMaster_Stats* BSP_SpiMaster_GetStats(void);

#endif /* BSP_SPI_MASTER_H */
