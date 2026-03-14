/**
 * @file   fault_recorder.h
 * @brief  高频故障录波环形缓冲 — SDRAM 架构头文件
 *
 * ✅ [Embedded-Engineer]
 * 本模块利用外部 SDRAM (32MB, 起始地址 0xC0000000) 构建一个
 * 超大环形缓冲区 (Ring Buffer)，用于存储 25.6kHz 采样率的全通道
 * 电压/电流波形数据。
 *
 * 信号流向：
 *
 *   EXTI1 ISR (25.6kHz)
 *       ↓ 每 25 个点填满一个 AdcBatch_t (约 1ms)
 *   FTS_ProtectTask
 *       ↓ memcpy 整个 Batch (400B) 作为一帧推入 Ring Buffer
 *   SDRAM Ring Buffer [Head → ... → Tail] (循环覆盖旧数据)
 *       ↓ 故障触发时
 *   锁定 [触发点前 100ms ~ 触发点后 100ms] 的波形索引
 *       ↓
 *   B 板通过 SPI4 分包读取故障录波数据
 */
#ifndef __FAULT_RECORDER_H__
#define __FAULT_RECORDER_H__

#include "main.h"
#include <stdint.h>
#include <string.h>

/* ═══════════ SDRAM 物理参数 ═══════════ */
/*
 * STM32F429 FMC Bank2 SDRAM 映射地址：0xC0000000
 * 总容量：32MB (IS42S16160J 或类似型号)
 *
 * ┌──────────────────────────────────────────────┐
 * │ 0xC0000000  ┌────────────────────────────┐   │
 * │             │ Ring Buffer 区域            │   │
 * │             │ (占用约 4.8MB)              │   │
 * │             │ = 12000 帧 × 400B/帧       │   │
 * │             └────────────────────────────┘   │
 * │ 0xC04B0000  ┌────────────────────────────┐   │
 * │             │ 故障快照冻结区 (可选)        │   │
 * │             └────────────────────────────┘   │
 * │ ...         (剩余空间可供其他模块使用)       │
 * └──────────────────────────────────────────────┘
 */
#define SDRAM_BASE_ADDR        0xC0000000UL           // ← SDRAM 起始地址
#define SDRAM_RECORDER_OFFSET  0x00000000UL           // ← 录波区域偏移 (从头开始)
#define SDRAM_RECORDER_ADDR    (SDRAM_BASE_ADDR + SDRAM_RECORDER_OFFSET)

/* ═══════════ 录波参数 ═══════════ */
#define POINTS_PER_BATCH       25                     // ← 每帧采样点数 (与 exti_ad7606 一致)
#define RECORDER_CHANNELS      8                      // ← AD7606 8 路电压通道
#define RECORDER_FRAME_SIZE    (RECORDER_CHANNELS * POINTS_PER_BATCH * sizeof(int16_t)) // ← 400 bytes/帧
#define RECORDER_TOTAL_FRAMES  12000                  // ← 环形缓冲总帧数 (12s @1ms/帧)
#define RECORDER_BUFFER_SIZE   (RECORDER_TOTAL_FRAMES * RECORDER_FRAME_SIZE)          // ← ~4.8MB

/* 故障前后各保留的帧数 (100ms = 100帧 @1ms/帧) */
#define RECORDER_PRE_FAULT_FRAMES    100              // ← 故障前 100ms
#define RECORDER_POST_FAULT_FRAMES   100              // ← 故障后 100ms

/* ═══════════ 录波帧定义 ═══════════ */

/**
 * 单帧波形数据 (与 AdcBatch_t 完全对齐)
 * 每帧 = 8 路 × 25 点 × 2 字节 = 400 字节
 */
typedef struct {
    int16_t ch[RECORDER_CHANNELS][POINTS_PER_BATCH];  // ← 8 路电压通道的 25 个采样点
} RecorderFrame_t;

/**
 * ✅ [Embedded-Engineer]
 * SDRAM 环形缓冲控制块 (仅在 SRAM 中维护指针，数据本体在 SDRAM)
 *
 * 指针运动示意 (时间从左到右)：
 *
 *   [旧数据被覆盖] ← Tail ──── ... ──── Head → [新数据写入]
 *                    ↑                    ↑
 *                    └── 最旧的有效帧      └── 最新写入的帧
 *
 * Head 追上 Tail 后，Tail 被推着前进 (旧数据自动丢弃)。
 */
typedef struct {
    RecorderFrame_t *base;           // ← SDRAM 中环形缓冲区首地址 (常量)
    uint32_t         capacity;       // ← 环形缓冲区总帧数 (常量 = RECORDER_TOTAL_FRAMES)
    volatile uint32_t head;          // ← 写入指针索引 (ISR/Task 写，只增不减)
    volatile uint32_t tail;          // ← 最旧有效帧索引 (被 head 推着走)
    volatile uint32_t count;         // ← 当前缓冲区中有效帧数 (0 ~ capacity)

    /* 故障冻结相关 */
    volatile uint8_t  triggered;     // ← 1 = 故障已触发，录波进入"后采集"阶段
    uint32_t          trigger_idx;   // ← 故障触发时 head 的值 (冻结点)
    uint32_t          post_count;    // ← 触发后已采集的帧数
    volatile uint8_t  frozen;        // ← 1 = 录波冻结完成，可供 B 板读取
} FaultRecorder_t;

/* ═══════════ 外部接口 ═══════════ */

/**
 * @brief  初始化故障录波器 (在 FTS_Protect_Init 中调用)
 */
void FaultRecorder_Init(void);

/**
 * @brief  推入一帧波形数据 (在每次 Ping-Pong 批次就绪时调用)
 * @param  frame  指向当前 AdcBatch_t 的指针 (400 字节)
 */
void FaultRecorder_PushFrame(const void *frame);

/**
 * @brief  触发故障录波冻结 (在状态机进入 TRIPPED/LOCKOUT 时调用)
 */
void FaultRecorder_Trigger(void);

/**
 * @brief  检查录波是否已冻结完成
 * @return 1 = 冻结完成可读取，0 = 仍在采集中
 */
uint8_t FaultRecorder_IsFrozen(void);

/**
 * @brief  获取故障前后波形的起始帧索引和帧数
 * @param  start_idx  输出：起始帧在环形缓冲中的索引
 * @param  frame_cnt  输出：总帧数 (前 + 后)
 */
void FaultRecorder_GetFaultWindow(uint32_t *start_idx, uint32_t *frame_cnt);

/**
 * @brief  读取指定索引的帧数据 (供 SPI4 分包传输)
 * @param  idx   环形缓冲中的帧索引
 * @param  dst   目标缓冲区 (至少 400 字节)
 */
void FaultRecorder_ReadFrame(uint32_t idx, void *dst);

#endif /* __FAULT_RECORDER_H__ */
