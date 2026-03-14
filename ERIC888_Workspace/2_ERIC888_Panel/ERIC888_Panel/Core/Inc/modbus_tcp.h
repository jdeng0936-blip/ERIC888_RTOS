/**
 * @file    modbus_tcp.h
 * @brief   Modbus TCP 最小协议栈 — SCADA 侧通信接口
 *
 * 支持功能码：
 *   FC03 - 读保持寄存器 (Read Holding Registers)
 *   FC06 - 写单寄存器 (Write Single Register)
 *   FC16 - 写多寄存器 (Write Multiple Registers)
 *
 * 寄存器表：
 *   0x0000-0x0005  三相电压 (Ua/Ub/Uc) + 三相电流 (Ia/Ib/Ic) — ADC 原始值
 *   0x0006-0x000B  上触头温度 ×3 + 下触头温度 ×3
 *   0x0010         FTS 保护状态 (fts_state)
 *   0x0011         故障类型 (fault_type)
 *   0x0012         运行拓扑 (topology)
 *   0x0013         备用电源可用 (backup_available)
 *   0x0014         主备相角差 (×10, 单位 0.1°)
 *   0x0015         录波冻结标志 (recorder_frozen)
 *   0x0020-0x002F  可写配置区 (温度门限、变比等)
 */

#ifndef MODBUS_TCP_H
#define MODBUS_TCP_H

#include <stdint.h>

/* Modbus TCP Unit ID (站号) */
#define MODBUS_UNIT_ID      1

/* 寄存器地址定义 */
#define REG_ADDR_UA         0x0000
#define REG_ADDR_UB         0x0001
#define REG_ADDR_UC         0x0002
#define REG_ADDR_IA         0x0003
#define REG_ADDR_IB         0x0004
#define REG_ADDR_IC         0x0005
#define REG_ADDR_TEMP_BASE  0x0006  /* 0x06~0x0B: 6 路温度 */
#define REG_ADDR_FTS_STATE  0x0010
#define REG_ADDR_FAULT_TYPE 0x0011
#define REG_ADDR_TOPOLOGY   0x0012
#define REG_ADDR_BACKUP_AV  0x0013
#define REG_ADDR_PHASE_DIFF 0x0014
#define REG_ADDR_REC_FROZEN 0x0015
#define REG_ADDR_CFG_BASE   0x0020  /* 可写配置区起始 */
#define REG_ADDR_CFG_END    0x002F

/* 寄存器总数 */
#define MODBUS_HOLDING_REG_COUNT  48  /* 0x0000 ~ 0x002F */

/**
 * @brief  处理一帧 Modbus TCP 请求，生成响应
 *
 * @param[in]  req       接收到的原始 TCP 数据
 * @param[in]  req_len   接收数据长度
 * @param[out] resp      响应缓冲区 (调用者分配, >= 260 bytes)
 * @param[out] resp_len  响应数据长度
 * @return     0=成功处理, -1=帧太短/不是 Modbus
 */
int ModbusTCP_Process(const uint8_t *req, int req_len,
                      uint8_t *resp, int *resp_len);

/**
 * @brief  从外部更新 ADC 数据到 Modbus 寄存器
 *         由 CommTask 或 DisplayTask 在新数据到达时调用
 */
void ModbusTCP_UpdateADC(const int16_t *ch, int ch_count,
                         const int16_t *internal_adc, int temp_count);

/**
 * @brief  从外部更新 FTS 保护状态到 Modbus 寄存器
 */
void ModbusTCP_UpdateFTS(uint8_t state, uint8_t fault, uint8_t topo,
                         uint8_t backup, int16_t phase_diff, uint8_t frozen);

#endif /* MODBUS_TCP_H */
