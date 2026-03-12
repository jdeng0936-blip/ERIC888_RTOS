/**
 * @file    modbus_rtu.c
 * @brief   Modbus RTU Slave implementation for ERIC888
 *
 *          Supported function codes:
 *          - 0x03: Read Holding Registers
 *          - 0x06: Write Single Register
 *
 *          Frame detection: UART idle interrupt (3.5 char gap at 115200 = ~0.3ms)
 *          Physical layer: USART3 RS485 half-duplex (PB8 TXEN)
 */
#include "modbus_rtu.h"
#include "bsp_rs485.h"
#include "dsp_calc.h"
#include "fault_recorder.h"
#include <string.h>

/* ========================= External References ========================= */
extern volatile uint32_t g_isr_cycles_max;
extern volatile uint32_t g_isr_count;

/* ========================= Constants ========================= */

#define MODBUS_RX_BUF_SIZE  256
#define MODBUS_TX_BUF_SIZE  256
#define MODBUS_RX_TIMEOUT   100   /* ms to wait for a complete frame */

/* Modbus function codes */
#define FC_READ_HOLDING    0x03
#define FC_WRITE_SINGLE    0x06

/* Modbus exception codes */
#define EX_ILLEGAL_FUNC    0x01
#define EX_ILLEGAL_ADDR    0x02
#define EX_ILLEGAL_DATA    0x03

/* Total readable registers */
#define MODBUS_MAX_REG     0x0046

/* ========================= Static State ========================= */

static UART_HandleTypeDef *s_huart;
static uint8_t s_slave_addr = MODBUS_DEFAULT_ADDR;

static uint8_t s_rx_buf[MODBUS_RX_BUF_SIZE];
static uint8_t s_tx_buf[MODBUS_TX_BUF_SIZE];

/* Writable configuration (mirrored from DSP_Protection_Config) */
static uint16_t s_ov_threshold  = 11000;  /* mV */
static uint16_t s_uv_threshold  = 100;    /* mV */
static uint16_t s_oc_threshold  = 3000;   /* mV */
static uint16_t s_thd_threshold = 1000;   /* 0.01% → 10.00% */
static uint16_t s_debounce      = 3;      /* blocks */

/* ========================= CRC-16/Modbus ========================= */

/**
 * @brief  Calculate Modbus CRC-16
 *         Polynomial: 0xA001 (reflected 0x8005)
 * @param  buf:  data buffer
 * @param  len:  data length
 * @retval CRC-16 value (little-endian)
 */
static uint16_t modbus_crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/* ========================= Register Read ========================= */

/**
 * @brief  Read a single holding register value
 * @param  addr: register address
 * @param  value: output value
 * @retval 0=OK, -1=illegal address
 */
static int read_register(uint16_t addr, uint16_t *value)
{
    const DSP_Results *dsp = DSP_GetResults();

    /* RMS[0..7]: float mV → uint16 */
    if (addr >= MODBUS_REG_RMS_BASE && addr < MODBUS_REG_RMS_BASE + 8) {
        int ch = addr - MODBUS_REG_RMS_BASE;
        *value = (uint16_t)(dsp->rms[ch] + 0.5f);
        return 0;
    }

    /* THD[0..7]: float % → uint16 (×100 for 0.01% resolution) */
    if (addr >= MODBUS_REG_THD_BASE && addr < MODBUS_REG_THD_BASE + 8) {
        int ch = addr - MODBUS_REG_THD_BASE;
        *value = (uint16_t)(dsp->thd[ch] * 100.0f + 0.5f);
        return 0;
    }

    /* Peak[0..7]: float mV → uint16 */
    if (addr >= MODBUS_REG_PEAK_BASE && addr < MODBUS_REG_PEAK_BASE + 8) {
        int ch = addr - MODBUS_REG_PEAK_BASE;
        *value = (uint16_t)(dsp->peak[ch] + 0.5f);
        return 0;
    }

    /* Fundamental[0..7]: float mV → uint16 */
    if (addr >= MODBUS_REG_FUND_BASE && addr < MODBUS_REG_FUND_BASE + 8) {
        int ch = addr - MODBUS_REG_FUND_BASE;
        *value = (uint16_t)(dsp->fundamental[ch] + 0.5f);
        return 0;
    }

    /* Status registers */
    switch (addr) {
        case MODBUS_REG_FAULT_FLAGS:
            *value = dsp->fault_flags;
            return 0;
        case MODBUS_REG_TRIP_REQ:
            *value = dsp->trip_requested;
            return 0;
        case MODBUS_REG_ISR_MAX:
            *value = (uint16_t)(g_isr_cycles_max & 0xFFFF);
            return 0;
        case MODBUS_REG_ISR_COUNT:
            *value = (uint16_t)(g_isr_count & 0xFFFF);
            return 0;
        case MODBUS_REG_FAULT_COUNT:
            *value = (uint16_t)(FaultRecorder_GetCount() & 0xFFFF);
            return 0;

        /* Writable config registers (also readable) */
        case MODBUS_REG_OV_THRESH:
            *value = s_ov_threshold;
            return 0;
        case MODBUS_REG_UV_THRESH:
            *value = s_uv_threshold;
            return 0;
        case MODBUS_REG_OC_THRESH:
            *value = s_oc_threshold;
            return 0;
        case MODBUS_REG_THD_THRESH:
            *value = s_thd_threshold;
            return 0;
        case MODBUS_REG_DEBOUNCE:
            *value = s_debounce;
            return 0;
        case MODBUS_REG_SLAVE_ADDR:
            *value = s_slave_addr;
            return 0;
        default:
            return -1;  /* Illegal address */
    }
}

/* ========================= Register Write ========================= */

/**
 * @brief  Write a single holding register
 * @param  addr: register address
 * @param  value: new value
 * @retval 0=OK, -1=illegal address, -2=illegal data
 */
static int write_register(uint16_t addr, uint16_t value)
{
    switch (addr) {
        case MODBUS_REG_OV_THRESH:
            s_ov_threshold = value;
            break;
        case MODBUS_REG_UV_THRESH:
            s_uv_threshold = value;
            break;
        case MODBUS_REG_OC_THRESH:
            s_oc_threshold = value;
            break;
        case MODBUS_REG_THD_THRESH:
            s_thd_threshold = value;
            break;
        case MODBUS_REG_DEBOUNCE:
            if (value == 0 || value > 100) return -2;
            s_debounce = value;
            break;
        case MODBUS_REG_SLAVE_ADDR:
            if (value < 1 || value > 247) return -2;
            s_slave_addr = (uint8_t)value;
            return 0;  /* No DSP update needed */
        default:
            return -1;
    }

    /* Apply updated thresholds to DSP module */
    DSP_Protection_Config cfg;
    cfg.ov_threshold_mv  = (float)s_ov_threshold;
    cfg.uv_threshold_mv  = (float)s_uv_threshold;
    cfg.oc_threshold_mv  = (float)s_oc_threshold;
    cfg.thd_threshold_pct = (float)s_thd_threshold / 100.0f;
    cfg.trip_debounce    = (uint8_t)s_debounce;
    DSP_SetProtection(&cfg);

    return 0;
}

/* ========================= Frame Handlers ========================= */

/**
 * @brief  Send an exception response
 */
static void send_exception(uint8_t func, uint8_t exception)
{
    s_tx_buf[0] = s_slave_addr;
    s_tx_buf[1] = func | 0x80;  /* Error flag */
    s_tx_buf[2] = exception;
    uint16_t crc = modbus_crc16(s_tx_buf, 3);
    s_tx_buf[3] = crc & 0xFF;
    s_tx_buf[4] = (crc >> 8) & 0xFF;
    BSP_RS485_Send(s_huart, s_tx_buf, 5, 50);
}

/**
 * @brief  Handle FC03: Read Holding Registers
 */
static void handle_read_holding(const uint8_t *frame, uint16_t frame_len)
{
    if (frame_len < 8) return;

    uint16_t start_addr = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t quantity   = ((uint16_t)frame[4] << 8) | frame[5];

    if (quantity == 0 || quantity > 125) {
        send_exception(FC_READ_HOLDING, EX_ILLEGAL_DATA);
        return;
    }

    /* Build response */
    s_tx_buf[0] = s_slave_addr;
    s_tx_buf[1] = FC_READ_HOLDING;
    s_tx_buf[2] = (uint8_t)(quantity * 2);  /* byte count */

    for (uint16_t i = 0; i < quantity; i++) {
        uint16_t val;
        if (read_register(start_addr + i, &val) != 0) {
            send_exception(FC_READ_HOLDING, EX_ILLEGAL_ADDR);
            return;
        }
        s_tx_buf[3 + i * 2]     = (val >> 8) & 0xFF;  /* big-endian */
        s_tx_buf[3 + i * 2 + 1] = val & 0xFF;
    }

    uint16_t resp_len = 3 + quantity * 2;
    uint16_t crc = modbus_crc16(s_tx_buf, resp_len);
    s_tx_buf[resp_len]     = crc & 0xFF;
    s_tx_buf[resp_len + 1] = (crc >> 8) & 0xFF;

    BSP_RS485_Send(s_huart, s_tx_buf, resp_len + 2, 50);
}

/**
 * @brief  Handle FC06: Write Single Register
 */
static void handle_write_single(const uint8_t *frame, uint16_t frame_len)
{
    if (frame_len < 8) return;

    uint16_t addr  = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t value = ((uint16_t)frame[4] << 8) | frame[5];

    int ret = write_register(addr, value);
    if (ret == -1) {
        send_exception(FC_WRITE_SINGLE, EX_ILLEGAL_ADDR);
        return;
    }
    if (ret == -2) {
        send_exception(FC_WRITE_SINGLE, EX_ILLEGAL_DATA);
        return;
    }

    /* Echo the request as response (Modbus spec) */
    memcpy(s_tx_buf, frame, 6);
    s_tx_buf[0] = s_slave_addr;  /* Ensure our address */
    uint16_t crc = modbus_crc16(s_tx_buf, 6);
    s_tx_buf[6] = crc & 0xFF;
    s_tx_buf[7] = (crc >> 8) & 0xFF;

    BSP_RS485_Send(s_huart, s_tx_buf, 8, 50);
}

/* ========================= Public API ========================= */

void Modbus_Init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    memset(s_rx_buf, 0, sizeof(s_rx_buf));
    memset(s_tx_buf, 0, sizeof(s_tx_buf));
}

void Modbus_Poll(void)
{
    /* Blocking receive: wait for Modbus frame
     * At 115200 baud, max 256-byte frame takes ~22ms
     * 3.5 char silence at 115200 = ~0.3ms → use 50ms timeout for safety */
    HAL_StatusTypeDef status;

    /* Receive up to 256 bytes with timeout */
    uint16_t rx_len = 0;

    /* Use single-byte polling with inter-character timeout for frame detection */
    uint32_t start_tick = HAL_GetTick();
    uint32_t last_byte_tick = 0;

    /* Ensure RS485 is in receive mode */
    HAL_GPIO_WritePin(RS485_TXEN_PORT, RS485_TXEN_PIN, GPIO_PIN_RESET);

    while (rx_len < MODBUS_RX_BUF_SIZE) {
        status = HAL_UART_Receive(s_huart, &s_rx_buf[rx_len], 1, 5);

        if (status == HAL_OK) {
            rx_len++;
            last_byte_tick = HAL_GetTick();
        } else {
            /* Timeout on byte receive */
            if (rx_len > 0) {
                /* We have some data and no new bytes for 5ms → frame complete */
                uint32_t gap = HAL_GetTick() - last_byte_tick;
                if (gap >= 4) {  /* 3.5 char time at 115200 ≈ 0.3ms, use 4ms margin */
                    break;  /* Frame complete */
                }
            }

            /* Overall timeout */
            if ((HAL_GetTick() - start_tick) > MODBUS_RX_TIMEOUT) {
                return;  /* No frame received */
            }
        }
    }

    /* Minimum Modbus RTU frame: addr(1) + func(1) + data(2+) + crc(2) = 6 bytes */
    if (rx_len < 6) return;

    /* Check slave address */
    if (s_rx_buf[0] != s_slave_addr && s_rx_buf[0] != 0) {
        return;  /* Not for us (0 = broadcast, respond but don't write is optional) */
    }

    /* Verify CRC */
    uint16_t rx_crc = ((uint16_t)s_rx_buf[rx_len - 1] << 8) | s_rx_buf[rx_len - 2];
    uint16_t calc_crc = modbus_crc16(s_rx_buf, rx_len - 2);
    if (rx_crc != calc_crc) {
        return;  /* CRC error — silently discard (Modbus spec) */
    }

    /* Dispatch by function code */
    uint8_t func = s_rx_buf[1];
    switch (func) {
        case FC_READ_HOLDING:
            handle_read_holding(s_rx_buf, rx_len);
            break;
        case FC_WRITE_SINGLE:
            handle_write_single(s_rx_buf, rx_len);
            break;
        default:
            send_exception(func, EX_ILLEGAL_FUNC);
            break;
    }
}
