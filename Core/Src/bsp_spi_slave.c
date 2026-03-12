/**
 * @file    bsp_spi_slave.c
 * @brief   SPI4 Slave communication driver implementation (v3.0 — 8-bit, DMA Normal)
 *
 *          "Always-Ready" DMA pattern:
 *          ───────────────────────────
 *          TX buffer is ALWAYS pre-loaded with the latest ADC data frame.
 *          When Master (B-board) clocks SPI, DMA sends TX automatically
 *          while simultaneously receiving Master's command into RX buffer.
 *
 *          After each transaction (DMA complete callback):
 *          1. Parse RX frame (B-board command)
 *          2. Refresh TX frame with latest double-buffer data
 *          3. Re-arm DMA for next transaction
 *
 *          This ensures A-board Slave is NEVER caught unprepared,
 *          regardless of when Master initiates transfer.
 */
#include "bsp_spi_slave.h"
#include "bsp_kswitch.h"
#include <string.h>

/* ========================= Internal state ========================= */

/** TX/RX frame buffers (4-byte aligned for DMA) */
static Eric888_SPI_Frame s_tx_frame __attribute__((aligned(4)));
static Eric888_SPI_Frame s_rx_frame __attribute__((aligned(4)));

/** Sequence counter for TX frames */
static uint8_t s_tx_seq = 0;

/** Last received sequence number (for gap detection) */
static uint8_t s_last_rx_seq = 0;

/** Communication statistics */
static SpiSlave_Stats s_stats = {0};

/** SPI handle cache */
static SPI_HandleTypeDef *s_hspi = NULL;

/* ========================= Implementation ========================= */

int BSP_SpiSlave_Init(SPI_HandleTypeDef *hspi)
{
    s_hspi = hspi;

    /* Pre-load TX with a heartbeat frame (no real data yet) */
    Eric888_InitFrame(&s_tx_frame, CMD_HEARTBEAT, s_tx_seq++);
    s_tx_frame.len = 0;
    Eric888_SealFrame(&s_tx_frame);

    /* Clear RX buffer */
    memset(&s_rx_frame, 0, sizeof(s_rx_frame));

    /* Start SPI4 full-duplex DMA: TX sends pre-loaded, RX captures command
     * v3.0: SPI is 8-bit mode, DMA is NORMAL mode — must re-arm after each xfer */
    if (HAL_SPI_TransmitReceive_DMA(hspi,
            (uint8_t *)&s_tx_frame,
            (uint8_t *)&s_rx_frame,
            sizeof(Eric888_SPI_Frame)) != HAL_OK) {
        return -1;
    }

    return 0;
}

void BSP_SpiSlave_RefreshTx(const Eric888_DoubleBuffer *db)
{
    /* Build a fresh ADC data frame from the double-buffer read side */
    const Eric888_ADC_Data *sample = Eric888_DB_GetReadBuf(
        (Eric888_DoubleBuffer *)db);

    Eric888_InitFrame(&s_tx_frame, CMD_READ_ADC, s_tx_seq++);

    /* Copy ADC data into payload */
    s_tx_frame.len = sizeof(Eric888_ADC_Data);
    if (s_tx_frame.len > ERIC888_MAX_PAYLOAD_LEN) {
        s_tx_frame.len = ERIC888_MAX_PAYLOAD_LEN;
    }
    memcpy(s_tx_frame.payload, sample, s_tx_frame.len);

    /* Seal with CRC-16 */
    Eric888_SealFrame(&s_tx_frame);
}

Eric888_SPI_Cmd BSP_SpiSlave_ProcessRx(void)
{
    /* Validate received frame */
    int result = Eric888_ValidateFrame(&s_rx_frame);

    if (result == -1) {
        /* Invalid header — could be first boot or noise, ignore */
        return CMD_NOP;
    }

    if (result == -2) {
        /* CRC error */
        s_stats.crc_errors++;
        return CMD_NOP;
    }

    s_stats.rx_count++;

    /* Check for sequence gaps */
    uint8_t expected_seq = s_last_rx_seq + 1;
    if (s_rx_frame.seq != expected_seq && s_stats.rx_count > 1) {
        s_stats.seq_gaps++;
    }
    s_last_rx_seq = s_rx_frame.seq;

    /* Parse command */
    Eric888_SPI_Cmd cmd = (Eric888_SPI_Cmd)s_rx_frame.cmd;

    switch (cmd) {
        case CMD_CTRL_RELAY:
            if (s_rx_frame.len >= sizeof(Eric888_Relay_Ctrl)) {
                Eric888_Relay_Ctrl *ctrl =
                    (Eric888_Relay_Ctrl *)s_rx_frame.payload;
                /* Execute relay command */
                for (int k = 0; k < 4; k++) {
                    if (ctrl->relay_mask & (1 << k)) {
                        BSP_KSwitch_Operate(k, KSWITCH_ACTION_ON,
                            ctrl->action ? ctrl->delay_ms : 20);
                    } else {
                        BSP_KSwitch_Operate(k, KSWITCH_ACTION_OFF,
                            ctrl->action ? ctrl->delay_ms : 20);
                    }
                }
            }
            break;

        case CMD_READ_ADC:
        case CMD_READ_STATUS:
        case CMD_HEARTBEAT:
            /* These are handled by TX mirror refresh */
            break;

        default:
            break;
    }

    return cmd;
}

void BSP_SpiSlave_DmaComplete(SPI_HandleTypeDef *hspi)
{
    if (hspi != s_hspi) return;

    s_stats.tx_count++;

    /* 1. Process received command from B-board */
    BSP_SpiSlave_ProcessRx();

    /* 2. Re-arm DMA for next transaction (v3.0: DMA_NORMAL requires re-arm) */
    HAL_SPI_TransmitReceive_DMA(s_hspi,
        (uint8_t *)&s_tx_frame,
        (uint8_t *)&s_rx_frame,
        sizeof(Eric888_SPI_Frame));
}

const SpiSlave_Stats* BSP_SpiSlave_GetStats(void)
{
    return &s_stats;
}
