/**
 * @file    modbus_tcp.c
 * @brief   Modbus TCP 最小协议栈实现
 *
 * 协议格式 (Modbus TCP/IP ADU):
 *   [MBAP Header 7 bytes] [PDU]
 *   MBAP: TransactionID(2) + ProtocolID(2=0x0000) + Length(2) + UnitID(1)
 *   PDU:  FunctionCode(1) + Data(N)
 *
 * 实现的功能码：
 *   FC03 (0x03) - Read Holding Registers
 *   FC06 (0x06) - Write Single Register
 *   FC16 (0x10) - Write Multiple Registers
 *
 * 异常码：
 *   0x01 - Illegal Function
 *   0x02 - Illegal Data Address
 *   0x03 - Illegal Data Value
 */

#include "modbus_tcp.h"
#include <string.h>

/* ================================================================
 * 内部寄存器表 (Holding Registers)
 *
 * 地址映射：
 *   0x0000~0x0005: 三相电压 + 三相电流 (来自 ADC)
 *   0x0006~0x000B: 6 路温度 (上触头×3 + 下触头×3)
 *   0x0010~0x0015: FTS 保护状态 / 故障 / 拓扑 / 备用电源 / 相差 / 录波
 *   0x0020~0x002F: 可写配置区
 * ================================================================ */
static uint16_t holding_regs[MODBUS_HOLDING_REG_COUNT];

/* ── MBAP 偏移 ── */
#define MBAP_TID_H      0   /* Transaction ID 高字节 */
#define MBAP_TID_L      1
#define MBAP_PID_H      2   /* Protocol ID (必须 0x0000) */
#define MBAP_PID_L      3
#define MBAP_LEN_H      4   /* 后续字节数 (含 UnitID + PDU) */
#define MBAP_LEN_L      5
#define MBAP_UID        6   /* Unit ID */
#define MBAP_SIZE       7

/* ── Modbus 功能码 ── */
#define FC_READ_HOLDING    0x03
#define FC_WRITE_SINGLE    0x06
#define FC_WRITE_MULTIPLE  0x10

/* ── 异常码 ── */
#define EX_ILLEGAL_FUNCTION   0x01
#define EX_ILLEGAL_ADDRESS    0x02
#define EX_ILLEGAL_VALUE      0x03

/* ================================================================
 * 工具函数
 * ================================================================ */

/* 大端读 16 位 */
static inline uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

/* 大端写 16 位 */
static inline void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

/* 构建异常响应 (MBAP + FC|0x80 + ExCode) */
static int build_exception(const uint8_t *req, uint8_t fc,
                           uint8_t ex_code, uint8_t *resp)
{
    /* 复制 Transaction ID + Protocol ID */
    memcpy(resp, req, 4);
    /* Length = 3 (UnitID + FC|0x80 + ExCode) */
    wr16(&resp[MBAP_LEN_H], 3);
    resp[MBAP_UID] = req[MBAP_UID];
    resp[MBAP_SIZE] = fc | 0x80;
    resp[MBAP_SIZE + 1] = ex_code;
    return MBAP_SIZE + 2;  /* 9 bytes 总长 */
}

/* ================================================================
 * FC03: 读保持寄存器
 *
 * 请求: FC(1) + StartAddr(2) + Quantity(2)  = 5 bytes PDU
 * 响应: FC(1) + ByteCount(1) + Data(N×2)
 * ================================================================ */
static int handle_fc03(const uint8_t *req, int req_len,
                       uint8_t *resp, int *resp_len)
{
    if (req_len < MBAP_SIZE + 5) {
        return build_exception(req, FC_READ_HOLDING, EX_ILLEGAL_VALUE, resp);
    }

    uint16_t start = rd16(&req[MBAP_SIZE + 1]);
    uint16_t qty   = rd16(&req[MBAP_SIZE + 3]);

    /* 校验地址范围: qty 1~125, start+qty <= 寄存器数 */
    if (qty == 0 || qty > 125 ||
        (start + qty) > MODBUS_HOLDING_REG_COUNT) {
        return build_exception(req, FC_READ_HOLDING, EX_ILLEGAL_ADDRESS, resp);
    }

    /* 构建响应 */
    uint8_t byte_count = (uint8_t)(qty * 2);

    /* MBAP Header */
    memcpy(resp, req, 4);  /* TID + PID */
    wr16(&resp[MBAP_LEN_H], (uint16_t)(3 + byte_count));  /* UnitID + FC + BC + Data */
    resp[MBAP_UID] = req[MBAP_UID];

    /* PDU */
    resp[MBAP_SIZE]     = FC_READ_HOLDING;
    resp[MBAP_SIZE + 1] = byte_count;

    /* 寄存器数据 (大端) */
    for (uint16_t i = 0; i < qty; i++) {
        wr16(&resp[MBAP_SIZE + 2 + i * 2], holding_regs[start + i]);
    }

    *resp_len = MBAP_SIZE + 2 + byte_count;
    return *resp_len;
}

/* ================================================================
 * FC06: 写单寄存器
 *
 * 请求: FC(1) + Addr(2) + Value(2) = 5 bytes PDU
 * 响应: 原样回传 (Echo)
 * ================================================================ */
static int handle_fc06(const uint8_t *req, int req_len,
                       uint8_t *resp, int *resp_len)
{
    if (req_len < MBAP_SIZE + 5) {
        return build_exception(req, FC_WRITE_SINGLE, EX_ILLEGAL_VALUE, resp);
    }

    uint16_t addr  = rd16(&req[MBAP_SIZE + 1]);
    uint16_t value = rd16(&req[MBAP_SIZE + 3]);

    /* 校验：只允许写配置区 0x0020~0x002F */
    if (addr < REG_ADDR_CFG_BASE || addr > REG_ADDR_CFG_END) {
        return build_exception(req, FC_WRITE_SINGLE, EX_ILLEGAL_ADDRESS, resp);
    }

    /* 写入寄存器 */
    holding_regs[addr] = value;

    /* 响应 = 请求原样回传 */
    memcpy(resp, req, MBAP_SIZE + 5);
    *resp_len = MBAP_SIZE + 5;
    return *resp_len;
}

/* ================================================================
 * FC16: 写多寄存器
 *
 * 请求: FC(1) + StartAddr(2) + Qty(2) + ByteCount(1) + Data(N×2)
 * 响应: FC(1) + StartAddr(2) + Qty(2)
 * ================================================================ */
static int handle_fc16(const uint8_t *req, int req_len,
                       uint8_t *resp, int *resp_len)
{
    if (req_len < MBAP_SIZE + 6) {
        return build_exception(req, FC_WRITE_MULTIPLE, EX_ILLEGAL_VALUE, resp);
    }

    uint16_t start     = rd16(&req[MBAP_SIZE + 1]);
    uint16_t qty       = rd16(&req[MBAP_SIZE + 3]);
    uint8_t  bc        = req[MBAP_SIZE + 5];

    /* 校验 */
    if (qty == 0 || qty > 123 || bc != qty * 2) {
        return build_exception(req, FC_WRITE_MULTIPLE, EX_ILLEGAL_VALUE, resp);
    }
    if (start < REG_ADDR_CFG_BASE ||
        (start + qty - 1) > REG_ADDR_CFG_END) {
        return build_exception(req, FC_WRITE_MULTIPLE, EX_ILLEGAL_ADDRESS, resp);
    }
    if (req_len < (int)(MBAP_SIZE + 6 + bc)) {
        return build_exception(req, FC_WRITE_MULTIPLE, EX_ILLEGAL_VALUE, resp);
    }

    /* 写入寄存器 */
    for (uint16_t i = 0; i < qty; i++) {
        holding_regs[start + i] = rd16(&req[MBAP_SIZE + 6 + i * 2]);
    }

    /* 构建响应 */
    memcpy(resp, req, 4);  /* TID + PID */
    wr16(&resp[MBAP_LEN_H], 6);  /* UnitID + FC + StartAddr + Qty */
    resp[MBAP_UID] = req[MBAP_UID];
    resp[MBAP_SIZE]     = FC_WRITE_MULTIPLE;
    wr16(&resp[MBAP_SIZE + 1], start);
    wr16(&resp[MBAP_SIZE + 3], qty);

    *resp_len = MBAP_SIZE + 5;
    return *resp_len;
}

/* ================================================================
 * 公共接口
 * ================================================================ */

/**
 * @brief  处理一帧 Modbus TCP 请求
 */
int ModbusTCP_Process(const uint8_t *req, int req_len,
                      uint8_t *resp, int *resp_len)
{
    *resp_len = 0;

    /* 最小长度校验: MBAP(7) + FC(1) = 8 */
    if (req_len < MBAP_SIZE + 1) return -1;

    /* Protocol ID 必须为 0x0000 (Modbus) */
    if (req[MBAP_PID_H] != 0x00 || req[MBAP_PID_L] != 0x00) return -1;

    /* 功能码路由 */
    uint8_t fc = req[MBAP_SIZE];

    switch (fc) {
        case FC_READ_HOLDING:
            return handle_fc03(req, req_len, resp, resp_len);

        case FC_WRITE_SINGLE:
            return handle_fc06(req, req_len, resp, resp_len);

        case FC_WRITE_MULTIPLE:
            return handle_fc16(req, req_len, resp, resp_len);

        default:
            /* 不支持的功能码 → 异常码 01 */
            *resp_len = build_exception(req, fc, EX_ILLEGAL_FUNCTION, resp);
            return *resp_len;
    }
}

/**
 * @brief  更新 ADC 数据到寄存器表
 */
void ModbusTCP_UpdateADC(const int16_t *ch, int ch_count,
                         const int16_t *internal_adc, int temp_count)
{
    /* 电压/电流通道 → 0x0000~0x0005 */
    int n = (ch_count > 6) ? 6 : ch_count;
    for (int i = 0; i < n; i++) {
        holding_regs[REG_ADDR_UA + i] = (uint16_t)ch[i];
    }

    /* 温度通道 → 0x0006~0x000B */
    int t = (temp_count > 6) ? 6 : temp_count;
    for (int i = 0; i < t; i++) {
        holding_regs[REG_ADDR_TEMP_BASE + i] = (uint16_t)internal_adc[i];
    }
}

/**
 * @brief  更新 FTS 保护状态到寄存器表
 */
void ModbusTCP_UpdateFTS(uint8_t state, uint8_t fault, uint8_t topo,
                         uint8_t backup, int16_t phase_diff, uint8_t frozen)
{
    holding_regs[REG_ADDR_FTS_STATE]  = state;
    holding_regs[REG_ADDR_FAULT_TYPE] = fault;
    holding_regs[REG_ADDR_TOPOLOGY]   = topo;
    holding_regs[REG_ADDR_BACKUP_AV]  = backup;
    holding_regs[REG_ADDR_PHASE_DIFF] = (uint16_t)phase_diff;
    holding_regs[REG_ADDR_REC_FROZEN] = frozen;
}
