/**
 * @file    bsp_spi_master.c
 * @brief   SPI4 Master driver for B-board (ERIC888 Panel)
 *          Implements v3.0 protocol with software NSS + PI11 handshake
 *
 *  Transaction sequence:
 *  1. CommTask builds TX frame (cmd + seq + CRC)
 *  2. Pull NSS (PE4) low
 *  3. Start SPI4 DMA full-duplex (sizeof(Eric888_SPI_Frame) bytes)
 *  4. DMA complete ISR → set semaphore
 *  5. CommTask wakes: pull NSS high, validate RX CRC
 */
#include "bsp_spi_master.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>

/* ========================= Private State ========================= */

static SPI_HandleTypeDef *s_hspi;
static SpiMaster_Stats s_stats;
static volatile TaskHandle_t s_comm_task_handle = NULL;

/** TX/RX frame buffers — DMA source/destination */
static Eric888_SPI_Frame s_tx_frame;
static Eric888_SPI_Frame s_rx_frame;

/** TX sequence counter (wraps at 255) */
static uint8_t s_tx_seq;

/** Binary semaphore: DMA complete notification */
static SemaphoreHandle_t s_dma_sem;
static StaticSemaphore_t s_dma_sem_buf;

/* ========================= NSS Control ========================= */

static inline void NSS_Low(void)
{
  HAL_GPIO_WritePin(SPI_NSS_PORT, SPI_NSS_PIN, GPIO_PIN_RESET);
}

static inline void NSS_High(void)
{
  HAL_GPIO_WritePin(SPI_NSS_PORT, SPI_NSS_PIN, GPIO_PIN_SET);
}

/* ========================= Public API ========================= */

int BSP_SpiMaster_Init(SPI_HandleTypeDef *hspi)
{
  if (hspi == NULL) return -1;

  s_hspi = hspi;
  s_tx_seq = 0;
  memset(&s_stats, 0, sizeof(s_stats));
  memset(&s_tx_frame, 0, sizeof(s_tx_frame));
  memset(&s_rx_frame, 0, sizeof(s_rx_frame));

  /* NSS idle high */
  NSS_High();

  /* Create binary semaphore for DMA completion */
  s_dma_sem = xSemaphoreCreateBinaryStatic(&s_dma_sem_buf);
  if (s_dma_sem == NULL) return -1;

  /* Enable PI11 EXTI interrupt (batch ready from A-board) */
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  return 0;
}

int BSP_SpiMaster_Transfer(Eric888_SPI_Cmd cmd,
                            const uint8_t *payload,
                            uint8_t payload_len)
{
  if (s_hspi == NULL) return -3;
  if (payload_len > ERIC888_MAX_PAYLOAD_LEN) return -3;

  /* Build TX frame */
  Eric888_InitFrame(&s_tx_frame, cmd, s_tx_seq++);
  if (payload != NULL && payload_len > 0) {
    memcpy(s_tx_frame.payload, payload, payload_len);
    s_tx_frame.len = payload_len;
  }
  Eric888_SealFrame(&s_tx_frame);

  /* Clear RX buffer */
  memset(&s_rx_frame, 0, sizeof(s_rx_frame));

  /* Pull NSS low — start transaction */
  NSS_Low();

  /* Start full-duplex DMA transfer */
  HAL_StatusTypeDef hal_ret;
  hal_ret = HAL_SPI_TransmitReceive_DMA(s_hspi,
                                         (uint8_t *)&s_tx_frame,
                                         (uint8_t *)&s_rx_frame,
                                         sizeof(Eric888_SPI_Frame));
  if (hal_ret != HAL_OK) {
    NSS_High();
    return -3;
  }

  /* Wait for DMA complete (timeout 50ms) */
  BaseType_t got = xSemaphoreTake(s_dma_sem, pdMS_TO_TICKS(50));
  NSS_High();

  if (got != pdTRUE) {
    /* Timeout: abort SPI */
    HAL_SPI_Abort(s_hspi);
    return -3;
  }

  s_stats.tx_count++;

  /* Validate received frame */
  if (s_rx_frame.header != ERIC888_FRAME_HEADER) {
    s_stats.header_errors++;
    return -2;
  }

  int crc_result = Eric888_ValidateFrame(&s_rx_frame);
  if (crc_result == -2) {
    s_stats.crc_errors++;
    return -1;
  }
  if (crc_result != 0) {
    s_stats.header_errors++;
    return -2;
  }

  /* Track sequence gaps */
  uint8_t expected_seq = s_stats.last_seq + 1;  /* wraps naturally */
  if (s_rx_frame.seq != expected_seq && s_stats.rx_count > 0) {
    s_stats.seq_gaps++;
  }
  s_stats.last_seq = s_rx_frame.seq;
  s_stats.rx_count++;

  return 0;
}

const Eric888_SPI_Frame* BSP_SpiMaster_GetRxFrame(void)
{
  return &s_rx_frame;
}

uint8_t BSP_SpiMaster_IsBatchReady(void)
{
  return s_stats.batch_pending;
}

void BSP_SpiMaster_ClearBatchReady(void)
{
  s_stats.batch_pending = 0;
}

void BSP_SpiMaster_DmaComplete(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance != s_hspi->Instance) return;

  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(s_dma_sem, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void BSP_SpiMaster_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == SPI_IRQ_PIN) {
    s_stats.batch_pending = 1;
    /* Master-level fix: Use Task Notification for microsecond response latency */
    if (s_comm_task_handle != NULL) {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      vTaskNotifyGiveFromISR(s_comm_task_handle, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
  }
}

void BSP_SpiMaster_SetCommTask(TaskHandle_t hTask)
{
  s_comm_task_handle = hTask;
  __DMB();  /* ensure ISR sees updated handle before next EXTI */
}

const SpiMaster_Stats* BSP_SpiMaster_GetStats(void)
{
  return &s_stats;
}
