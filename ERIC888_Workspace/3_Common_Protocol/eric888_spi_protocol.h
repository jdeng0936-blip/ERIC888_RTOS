/**
 ******************************************************************************
 * @file    eric888_spi_protocol.h
 * @brief   ERIC888 SPI protocol v3.0 — Industrial hardened
 *          A-board(Slave) <--SPI4--> B-board(Master)
 * @version 3.0.0
 * @date    2026-03-11
 ******************************************************************************
 * v3.0 Industrial fixes:
 *   1. SPI switched to 8-bit mode (eliminates 16-bit struct alignment trap)
 *   2. Batch transfer: 512-sample ring buffer, ~20ms per batch (not per-sample)
 *   3. Software NSS + PI11 IRQ handshake (noise immune)
 *   4. Non-circular DMA with TX snapshot (no half-new/half-old tearing)
 *   5. CRC-16/MODBUS checksum retained from v2.0
 *   6. Sequence numbers retained from v2.0
 ******************************************************************************
 */

#ifndef __ERIC888_SPI_PROTOCOL_H
#define __ERIC888_SPI_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <string.h>

/* __DMB() for memory barrier between ISR and Task */
#if defined(__GNUC__) || defined(__clang__)
  #define ERIC888_DMB()  __asm volatile ("dmb" ::: "memory")
#else
  #include "cmsis_compiler.h"
  #define ERIC888_DMB()  __DMB()
#endif

/* ========================= Version ========================= */
#define ERIC888_PROTOCOL_VERSION_MAJOR 3
#define ERIC888_PROTOCOL_VERSION_MINOR 0
#define ERIC888_PROTOCOL_VERSION_PATCH 0

/* ========================= Constants ========================= */
#define ERIC888_FRAME_HEADER     0xEB90U
#define ERIC888_MAX_PAYLOAD_LEN  64U
#define ERIC888_ADC_CHANNELS     8U
#define ERIC888_INTERNAL_ADC_CH  6U
#define ERIC888_BATCH_SIZE       512U   /* Samples per batch (~20ms @ 25.6kHz) */

/* ========================= Commands ========================= */
typedef enum {
  CMD_NOP           = 0x00,
  CMD_READ_ADC      = 0x01,  /**< Read single latest sample */
  CMD_READ_BATCH    = 0x03,  /**< Read batch (512 samples from SDRAM) */
  CMD_READ_STATUS   = 0x02,
  CMD_CTRL_RELAY    = 0x10,
  CMD_HEARTBEAT     = 0xAA,
  CMD_ACK           = 0xF0,
  CMD_NACK          = 0xF1,
  CMD_BATCH_READY   = 0xB0,  /**< A-board notifies: batch buffer ready */
} Eric888_SPI_Cmd;

/* ========================= Frame v3 (8-bit safe) ========================= */
/**
 * @brief  SPI frame — ALL fields are naturally 8-bit aligned
 *         No padding needed. Works with both 8-bit and 16-bit SPI.
 *
 *  | header(2B) | cmd(1B) | seq(1B) | len(1B) | reserved(1B) | payload(0~64B) | crc16(2B) |
 *
 *  Total: 6 + payload_len + 2 = always even bytes
 */
typedef struct __attribute__((packed)) {
  uint16_t header;    /**< 0xEB90 */
  uint8_t  cmd;       /**< Command byte */
  uint8_t  seq;       /**< Sequence 0~255 */
  uint8_t  len;       /**< Payload length */
  uint8_t  reserved;  /**< 0x00, ensures header block = 6 bytes (even) */
  uint8_t  payload[ERIC888_MAX_PAYLOAD_LEN];
  uint16_t crc16;     /**< CRC-16/MODBUS */
} Eric888_SPI_Frame;

/* ========================= Data Structures ========================= */

/** Single ADC sample (used in double-buffer and batch ring) */
typedef struct __attribute__((packed)) {
  uint32_t timestamp_ms;
  int16_t  ch[ERIC888_ADC_CHANNELS];
  uint16_t internal_adc[ERIC888_INTERNAL_ADC_CH];
  uint16_t sample_count;
} Eric888_ADC_Data;  /* = 4 + 16 + 12 + 2 = 34 bytes, all even-aligned */

/** Relay control (B-board to A-board) */
typedef struct __attribute__((packed)) {
  uint8_t  relay_mask;
  uint8_t  action;
  uint16_t delay_ms;
} Eric888_Relay_Ctrl;  /* 4 bytes, even */

/** System status */
typedef struct __attribute__((packed)) {
  uint8_t  relay_state;
  uint8_t  fault_code;
  uint8_t  adc_ready;
  uint8_t  cpu_load_percent;
  uint32_t uptime_ms;
  uint32_t sample_rate_hz;
  uint16_t lost_frames;
  uint16_t isr_cycles_max;   /**< Worst-case ISR cycles (DWT) */
  uint16_t batch_write_idx;  /**< Current batch ring position */
  uint8_t  protocol_ver;
  uint8_t  _pad;
} Eric888_Status;  /* 20 bytes, even */

/* ========================= CRC-16/MODBUS ========================= */

static inline uint16_t Eric888_CRC16(const uint8_t *data, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001)
        crc = (crc >> 1) ^ 0xA001;
      else
        crc >>= 1;
    }
  }
  return crc;
}

static inline uint16_t Eric888_CalcCRC(const Eric888_SPI_Frame *frame) {
  return Eric888_CRC16(&frame->cmd, 4 + frame->len);
}

static inline void Eric888_InitFrame(Eric888_SPI_Frame *frame,
                                      Eric888_SPI_Cmd cmd,
                                      uint8_t seq) {
  memset(frame, 0, sizeof(Eric888_SPI_Frame));
  frame->header   = ERIC888_FRAME_HEADER;
  frame->cmd      = (uint8_t)cmd;
  frame->seq      = seq;
  frame->len      = 0;
  frame->reserved = 0;
}

static inline void Eric888_SealFrame(Eric888_SPI_Frame *frame) {
  frame->crc16 = Eric888_CalcCRC(frame);
}

static inline int Eric888_ValidateFrame(const Eric888_SPI_Frame *frame) {
  if (frame->header != ERIC888_FRAME_HEADER) return -1;
  if (frame->len > ERIC888_MAX_PAYLOAD_LEN) return -1;
  if (frame->crc16 != Eric888_CalcCRC(frame)) return -2;
  return 0;
}

/* ========================= Double Buffer (ISR/Task) ========================= */

typedef struct {
  Eric888_ADC_Data buf[2];
  volatile uint8_t write_idx;
  volatile uint8_t fresh;
} Eric888_DoubleBuffer;

static inline void Eric888_DB_Init(Eric888_DoubleBuffer *db) {
  memset(db, 0, sizeof(Eric888_DoubleBuffer));
}

static inline Eric888_ADC_Data* Eric888_DB_GetWriteBuf(Eric888_DoubleBuffer *db) {
  return &db->buf[db->write_idx];
}

static inline void Eric888_DB_Swap(Eric888_DoubleBuffer *db) {
  db->write_idx ^= 1;
  ERIC888_DMB();          /* ensure all data writes complete before fresh flag */
  db->fresh = 1;
}

static inline const Eric888_ADC_Data* Eric888_DB_GetReadBuf(Eric888_DoubleBuffer *db) {
  db->fresh = 0;
  ERIC888_DMB();          /* ensure fresh=0 is visible before reading buffer */
  return &db->buf[db->write_idx ^ 1];
}

static inline uint8_t Eric888_DB_HasFresh(const Eric888_DoubleBuffer *db) {
  return db->fresh;
}

/* ========================= Batch Ring Buffer (SDRAM) ========================= */
/**
 * @brief  512-sample ring buffer in SDRAM for batch transfer
 *
 *         ISR fills one sample per 39us (25.6kHz)
 *         When 512 samples accumulated (~20ms), A-board asserts PI11 IRQ
 *         B-board reads entire batch via SPI DMA in one shot
 *
 *         Ping-Pong: while B-board reads batch[read_bank],
 *                    ISR fills batch[write_bank]
 */
typedef struct {
  Eric888_ADC_Data samples[ERIC888_BATCH_SIZE]; /**< 512 x 34B = 17408B */
  volatile uint16_t count;                      /**< Samples written in current bank */
} Eric888_BatchBank;

typedef struct {
  Eric888_BatchBank bank[2];     /**< Two banks for ping-pong */
  volatile uint8_t  write_bank;  /**< ISR writes to this bank (0 or 1) */
  volatile uint8_t  read_ready;  /**< 1 = read bank has full data */
} Eric888_BatchRing;

/** Initialize batch ring (call once at startup) */
static inline void Eric888_Batch_Init(Eric888_BatchRing *ring) {
  ring->write_bank = 0;
  ring->read_ready = 0;
  ring->bank[0].count = 0;
  ring->bank[1].count = 0;
}

/**
 * @brief  Push one sample into batch ring (called from EXTI1 ISR)
 * @retval 1 = batch full (trigger PI11 IRQ to B-board), 0 = still filling
 */
static inline int Eric888_Batch_Push(Eric888_BatchRing *ring,
                                      const Eric888_ADC_Data *sample) {
  Eric888_BatchBank *wb = &ring->bank[ring->write_bank];
  if (wb->count < ERIC888_BATCH_SIZE) {
    wb->samples[wb->count] = *sample;
    wb->count++;
  }
  if (wb->count >= ERIC888_BATCH_SIZE) {
    /* Bank full: swap banks */
    ERIC888_DMB();        /* ensure all 512 samples are flushed before swap */
    ring->read_ready = 1;
    ring->write_bank ^= 1;
    ring->bank[ring->write_bank].count = 0;  /* Reset new write bank */
    return 1;  /* Signal: batch ready */
  }
  return 0;
}

/** Get read-side batch bank (for SPI DMA transfer to B-board) */
static inline const Eric888_BatchBank* Eric888_Batch_GetReadBank(
    Eric888_BatchRing *ring) {
  ring->read_ready = 0;
  return &ring->bank[ring->write_bank ^ 1];
}

#ifdef __cplusplus
}
#endif

#endif /* __ERIC888_SPI_PROTOCOL_H */
