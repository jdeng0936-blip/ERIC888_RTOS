/**
 * @file    fault_recorder.c
 * @brief   Fault event recorder implementation (black box)
 *
 *          Data flow:
 *          1. CalcTask detects fault → FaultRecorder_PostEvent()
 *          2. FaultEvent + waveform enqueued to FreeRTOS queue
 *          3. StoreTask wakes → FaultRecorder_ProcessTask()
 *          4. Write fault record to SPI Flash (ring buffer)
 *
 *          Flash layout:
 *          Sector 0 (4KB):  Metadata { magic, total_count, write_index }
 *          Sector 1 (4KB):  Fault record #0
 *          Sector 2 (4KB):  Fault record #1
 *          ...
 *          Sector N (4KB):  Fault record #N-1
 */
#include "fault_recorder.h"
#include "bsp_spiflash.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <string.h>

/* ========================= Internal Types ========================= */

/* Metadata stored in Flash Sector 0 */
typedef struct {
    uint32_t magic;           /* 0xEC880001 = valid metadata */
    uint32_t total_count;     /* lifetime fault count */
    uint32_t write_index;     /* next record slot (0..FAULT_MAX_RECORDS-1) */
    uint32_t reserved;
} FaultMeta;

#define FAULT_META_MAGIC  0xEC880001

/* Internal queue item: event + waveform copy */
typedef struct {
    FaultEvent event;
    int16_t    waveform[512]; /* single channel waveform snapshot */
    uint16_t   num_samples;
    uint16_t   pad;
} FaultQueueItem;

/* ========================= Static State ========================= */

static QueueHandle_t s_fault_queue;
static FaultMeta     s_meta;
/* Working buffer for Flash page writes (256-byte aligned) */
static uint8_t       s_page_buf[256] __attribute__((aligned(4)));

/* ========================= Private Functions ========================= */

/**
 * @brief  Read metadata from Flash Sector 0
 */
static void meta_read(void)
{
    BSP_SpiFlash_Read(0, (uint8_t *)&s_meta, sizeof(FaultMeta));
    if (s_meta.magic != FAULT_META_MAGIC) {
        /* First boot or corrupted metadata — initialize */
        s_meta.magic       = FAULT_META_MAGIC;
        s_meta.total_count = 0;
        s_meta.write_index = 0;
        s_meta.reserved    = 0;
    }
}

/**
 * @brief  Write metadata to Flash Sector 0
 *         Note: must erase sector first (Flash can only write 1→0)
 */
static void meta_write(void)
{
    BSP_SpiFlash_EraseSector(0);
    BSP_SpiFlash_WritePage(0, (uint8_t *)&s_meta, sizeof(FaultMeta));
}

/**
 * @brief  Calculate Flash address for a given record index
 */
static uint32_t record_addr(uint32_t index)
{
    return FAULT_DATA_BASE_ADDR + (index * FAULT_SECTOR_SIZE);
}

/**
 * @brief  Write a fault record to Flash
 */
static void write_record(const FaultQueueItem *item)
{
    uint32_t addr = record_addr(s_meta.write_index);

    /* Erase the target sector (required before writing) */
    BSP_SpiFlash_EraseSector(addr);

    /* Build record header */
    FaultRecordHeader hdr;
    hdr.magic            = FAULT_RECORD_MAGIC;
    hdr.record_id        = s_meta.total_count;
    hdr.timestamp_ms     = item->event.timestamp_ms;
    hdr.fault_flags      = item->event.fault_flags;
    hdr.fault_channel    = item->event.fault_channel;
    hdr.waveform_samples = item->num_samples;
    hdr.dsp_snapshot     = item->event.dsp_snapshot;

    /* Write header (may span multiple pages if > 256 bytes) */
    uint32_t hdr_size = sizeof(FaultRecordHeader);
    uint32_t offset = 0;

    while (offset < hdr_size) {
        uint16_t chunk = (hdr_size - offset > 256) ? 256 : (uint16_t)(hdr_size - offset);
        memcpy(s_page_buf, (uint8_t *)&hdr + offset, chunk);
        BSP_SpiFlash_WritePage(addr + offset, s_page_buf, chunk);
        offset += chunk;
    }

    /* Write waveform data after header */
    uint32_t wave_size = item->num_samples * sizeof(int16_t);
    uint32_t wave_offset = 0;
    uint32_t wave_addr = addr + hdr_size;

    while (wave_offset < wave_size) {
        uint16_t chunk = (wave_size - wave_offset > 256) ? 256 : (uint16_t)(wave_size - wave_offset);
        memcpy(s_page_buf, (uint8_t *)item->waveform + wave_offset, chunk);
        BSP_SpiFlash_WritePage(wave_addr + wave_offset, s_page_buf, chunk);
        wave_offset += chunk;
    }

    /* Update metadata */
    s_meta.total_count++;
    s_meta.write_index = (s_meta.write_index + 1) % FAULT_MAX_RECORDS;
    meta_write();
}

/* ========================= Public API ========================= */

void FaultRecorder_Init(void)
{
    /* Create queue: depth 4 (CalcTask should not produce faster than StoreTask consumes) */
    s_fault_queue = xQueueCreate(4, sizeof(FaultQueueItem));

    /* Read existing metadata from Flash */
    meta_read();
}

void FaultRecorder_PostEvent(const FaultEvent *event,
                             const int16_t *waveform, uint16_t num_samples)
{
    if (s_fault_queue == NULL) return;
    if (num_samples > 512) num_samples = 512;

    FaultQueueItem item;
    item.event = *event;
    item.num_samples = num_samples;
    if (waveform != NULL && num_samples > 0) {
        memcpy(item.waveform, waveform, num_samples * sizeof(int16_t));
    } else {
        item.num_samples = 0;
    }
    item.pad = 0;

    /* Non-blocking send: if queue is full, drop oldest (overwrite) */
    /* Use xQueueSend with 0 timeout — if full, we accept the loss */
    xQueueSend(s_fault_queue, &item, 0);
}

void FaultRecorder_ProcessTask(void)
{
    FaultQueueItem item;

    /* Block forever waiting for fault events */
    if (xQueueReceive(s_fault_queue, &item, portMAX_DELAY) == pdTRUE) {
        write_record(&item);
    }
}

uint32_t FaultRecorder_GetCount(void)
{
    return s_meta.total_count;
}
