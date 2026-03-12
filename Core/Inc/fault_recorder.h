/**
 * @file    fault_recorder.h
 * @brief   Fault event recorder (black box) for protection trip events
 *
 *          Architecture:
 *          ┌──────────┐   FreeRTOS Queue   ┌───────────┐   SPI Flash
 *          │ CalcTask ├──────────────────►  │ StoreTask ├──────────►  W25Qxx
 *          └──────────┘   FaultEvent        └───────────┘   4KB/record
 *
 *          Each fault record contains:
 *          - Timestamp (HAL_GetTick ms)
 *          - Fault type flags
 *          - DSP results snapshot (RMS/THD/Peak per channel)
 *          - Last batch waveform (512 samples × 8ch × 2 bytes = 8KB)
 *            → compressed to 1 channel (the faulted channel) = 1KB
 *
 *          Storage layout in SPI Flash:
 *          - Sector 0 (4KB): Metadata header (record count, write pointer)
 *          - Sector 1..N:    Fault records (ring buffer, oldest overwritten)
 */
#ifndef FAULT_RECORDER_H
#define FAULT_RECORDER_H

#include "stm32f4xx_hal.h"
#include "dsp_calc.h"

/* Maximum fault records in SPI Flash (W25Q128 = 16MB, using first 1MB) */
#define FAULT_MAX_RECORDS     250
#define FAULT_SECTOR_SIZE     4096     /* W25Qxx sector = 4KB */
#define FAULT_META_SECTOR     0        /* Sector 0 = metadata */
#define FAULT_DATA_BASE_ADDR  FAULT_SECTOR_SIZE  /* Records start at 4KB offset */

/* Fault event posted from CalcTask → StoreTask via queue */
typedef struct {
    uint32_t    timestamp_ms;          /* HAL_GetTick() at fault detection */
    uint8_t     fault_flags;           /* channel fault mask */
    uint8_t     fault_channel;         /* primary faulted channel (0-7) */
    uint8_t     reserved[2];           /* alignment padding */
    DSP_Results dsp_snapshot;          /* full DSP results at time of fault */
} FaultEvent;

/* Fault record header stored in Flash (per-record) */
typedef struct {
    uint32_t    magic;                 /* 0xFA17FA17 = valid record */
    uint32_t    record_id;             /* sequential record number */
    uint32_t    timestamp_ms;          /* fault timestamp */
    uint8_t     fault_flags;           /* fault channel mask */
    uint8_t     fault_channel;         /* primary channel */
    uint16_t    waveform_samples;      /* number of waveform samples (512) */
    DSP_Results dsp_snapshot;          /* DSP results */
    /* Followed by waveform data: int16_t[waveform_samples] */
} FaultRecordHeader;

#define FAULT_RECORD_MAGIC  0xFA17FA17

/**
 * @brief  Initialize fault recorder (create queue, read Flash metadata)
 */
void FaultRecorder_Init(void);

/**
 * @brief  Post a fault event from CalcTask (non-blocking)
 *         Called when DSP detects a protection trip
 * @param  event: fault event data
 * @param  waveform: pointer to the 512-sample batch that triggered the fault
 *                   (single channel, int16_t[512])
 */
void FaultRecorder_PostEvent(const FaultEvent *event,
                             const int16_t *waveform, uint16_t num_samples);

/**
 * @brief  StoreTask main loop: blocks on queue, writes to Flash
 *         Call this from StartTaskStore() in an infinite loop
 */
void FaultRecorder_ProcessTask(void);

/**
 * @brief  Get total number of recorded faults
 */
uint32_t FaultRecorder_GetCount(void);

#endif /* FAULT_RECORDER_H */
