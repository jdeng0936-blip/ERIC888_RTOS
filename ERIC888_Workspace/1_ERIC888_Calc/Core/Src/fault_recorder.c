/**
 * @file   fault_recorder.c
 * @brief  高频故障录波环形缓冲 — SDRAM 实现
 *
 * ✅ [Embedded-Engineer]
 * 本模块在外部 SDRAM 中维护一个巨大的环形缓冲区，持续记录 25.6kHz 全通道波形。
 * 当故障触发时，自动锁定 [故障前 100ms ~ 故障后 100ms] 的波形窗口。
 *
 * 核心设计原则：
 *   ① CPU 不搬运 25.6kHz 的每个采样点！只搬运每 25 点一批次的 AdcBatch_t（~400B）
 *   ② 用 Head/Tail 指针循环覆盖旧数据，SDRAM 永远不会爆满
 *   ③ 故障触发后还要继续录 100ms（后采集），然后冻结，供 B 板通过 SPI4 读取
 *
 * 内存占用：
 *   12000 帧 × 400 字节/帧 = 4,800,000 字节 ≈ 4.58 MB
 *   SDRAM 总容量 32MB，占用率仅 14.3%，裕量极大
 *
 * 调用时序：
 *
 *   ┌─────────────┐    每 1ms     ┌───────────────┐
 *   │ EXTI1 ISR   │─────────────→│ FTS_ProtectTask│
 *   │ (填满Batch)  │              │  ①推帧到Ring   │
 *   └─────────────┘              │  ②检测故障     │
 *                                │  ③触发冻结     │
 *                                └───────────────┘
 *                                       ↓
 *                                ┌───────────────┐
 *                                │ SDRAM Ring Buf │
 *                                │ [T...H] 循环   │
 *                                └───────────────┘
 */

#include "fault_recorder.h"

/* ═══════════ 全局实例 ═══════════ */

static FaultRecorder_t g_recorder;     // ← 控制块在 SRAM 中，数据在 SDRAM 中

/* ═══════════ 初始化 ═══════════ */

/**
 * @brief  初始化故障录波器
 *
 * 将控制块清零，设置 SDRAM 区域的基地址指针。
 * 注意：不需要清零 SDRAM 数据区 (4.8MB 太大了，且会被覆盖)。
 *
 * 调用时机：在 FTS_Protect_Init() 中调用。
 */
void FaultRecorder_Init(void)
{
    g_recorder.base     = (RecorderFrame_t *)SDRAM_RECORDER_ADDR; // ← 指向 SDRAM 0xC0000000
    g_recorder.capacity = RECORDER_TOTAL_FRAMES;                  // ← 12000 帧
    g_recorder.head     = 0;                                      // ← 写指针归零
    g_recorder.tail     = 0;                                      // ← 读指针归零
    g_recorder.count    = 0;                                      // ← 有效帧数归零

    g_recorder.triggered  = 0;                                    // ← 未触发
    g_recorder.trigger_idx = 0;
    g_recorder.post_count  = 0;
    g_recorder.frozen      = 0;                                   // ← 未冻结
}

/* ═══════════ 推帧 (每 1ms 调用一次) ═══════════ */

/**
 * @brief  将一帧波形数据推入环形缓冲区
 *
 * @param  frame  指向当前 AdcBatch_t 的指针 (必须是 400 字节对齐的数据)
 *
 * ✅ [Embedded-Engineer] 环形缓冲核心逻辑：
 *
 *   状态A — 缓冲区未满 (count < capacity)：
 *     Head 前进, Tail 不动, count 增加
 *     [Tail ──── data ──── Head]
 *
 *   状态B — 缓冲区已满 (count == capacity)：
 *     Head 前进, Tail 也跟着前进 (最旧的帧被覆盖)
 *     [Tail ──── data ──── Head]
 *        ↑                   ↑
 *        └─ 被新数据覆盖      └─ 新数据写入
 *
 *   状态C — 故障已触发 (triggered == 1)：
 *     继续写入，但同时计数 post_count
 *     当 post_count >= RECORDER_POST_FAULT_FRAMES 时冻结
 */
void FaultRecorder_PushFrame(const void *frame)
{
    /* 如果已经冻结，不再写入 (保护故障波形不被覆盖) */
    if (g_recorder.frozen) {
        return;                                                    // ← 冻结后的帧全部丢弃
    }

    /* 将 400 字节的帧数据 memcpy 到 SDRAM 中 Head 指向的位置 */
    memcpy(&g_recorder.base[g_recorder.head],                     // ← 目标：SDRAM 中的帧槽
           frame,                                                  // ← 来源：当前批次数据
           sizeof(RecorderFrame_t));                               // ← 大小：400 字节

    /* Head 前进一步 (环形回绕) */
    g_recorder.head++;
    if (g_recorder.head >= g_recorder.capacity) {
        g_recorder.head = 0;                                       // ← 到达末尾，回到起点
    }

    /* 更新有效帧计数和 Tail 的位置 */
    if (g_recorder.count < g_recorder.capacity) {
        g_recorder.count++;                                        // ← 缓冲区未满，count 增加
    } else {
        /* 缓冲区已满：Tail 被 Head 推着走 (最旧的帧被自然覆盖) */
        g_recorder.tail++;
        if (g_recorder.tail >= g_recorder.capacity) {
            g_recorder.tail = 0;                                   // ← Tail 也环形回绕
        }
    }

    /* 如果故障已触发，计数后采集帧数 */
    if (g_recorder.triggered) {
        g_recorder.post_count++;                                   // ← 每帧 +1
        if (g_recorder.post_count >= RECORDER_POST_FAULT_FRAMES) {
            g_recorder.frozen = 1;                                 // ← 后采集完成！冻结！
            /*
             * 此刻 SDRAM 中保留了：
             *   [trigger_idx - 100帧] ~ [trigger_idx + 100帧]
             *   即故障前 100ms + 故障后 100ms 的完整波形
             */
        }
    }
}

/* ═══════════ 故障触发 ═══════════ */

/**
 * @brief  触发故障录波冻结
 *
 * 当 FTS_ProtectTask 状态机进入 TRIPPED 或 LOCKOUT 时调用此函数。
 * 它会：
 *   ① 记录当前 Head 位置作为"触发点"
 *   ② 设置 triggered 标志，让 PushFrame 开始计数后采集帧
 *   ③ 后采集 100 帧后自动冻结 (frozen = 1)
 *
 * 注意：此函数只能调用一次！重复调用无效 (防止双重触发)。
 */
void FaultRecorder_Trigger(void)
{
    if (g_recorder.triggered) return;                              // ← 防止重复触发

    g_recorder.triggered  = 1;                                     // ← 进入后采集模式
    g_recorder.trigger_idx = g_recorder.head;                      // ← 锁定触发点
    g_recorder.post_count  = 0;                                    // ← 后采集计数归零
}

/* ═══════════ 冻结状态查询 ═══════════ */

/**
 * @brief  检查录波是否已冻结完成
 * @return 1 = 故障前后波形已全部采集完毕，可供读取
 */
uint8_t FaultRecorder_IsFrozen(void)
{
    return g_recorder.frozen;
}

/* ═══════════ 故障窗口获取 ═══════════ */

/**
 * @brief  获取故障前后波形的起始帧索引和总帧数
 *
 * @param  start_idx  输出：故障窗口起始帧在环形缓冲中的索引
 * @param  frame_cnt  输出：故障窗口总帧数 (前100 + 后100 = 200帧)
 *
 * ✅ [Embedded-Engineer] 窗口计算逻辑：
 *
 *   trigger_idx = 故障发生时 Head 的位置
 *
 *   start = trigger_idx - RECORDER_PRE_FAULT_FRAMES
 *         = 触发点往回退 100 帧 (故障前 100ms)
 *
 *   如果 start < 0，需要环形回绕 (加上 capacity)
 *
 *   total = PRE + POST = 100 + 100 = 200 帧
 *         = 200ms 的完整故障录波
 */
void FaultRecorder_GetFaultWindow(uint32_t *start_idx, uint32_t *frame_cnt)
{
    /* 起始索引 = 触发点倒退 100 帧 */
    int32_t start = (int32_t)g_recorder.trigger_idx - RECORDER_PRE_FAULT_FRAMES;
    if (start < 0) {
        start += (int32_t)g_recorder.capacity;                     // ← 环形回绕
    }
    *start_idx = (uint32_t)start;
    *frame_cnt = RECORDER_PRE_FAULT_FRAMES + RECORDER_POST_FAULT_FRAMES; // ← 200 帧
}

/* ═══════════ 帧数据读取 ═══════════ */

/**
 * @brief  读取指定索引的帧数据 (供 SPI4 分包传输给 B 板)
 *
 * @param  idx  环形缓冲中的帧索引 (需要由调用者保证在有效范围内)
 * @param  dst  目标缓冲区 (至少 400 字节)
 *
 * 使用方法 (B 板读取故障录波时)：
 *
 *   uint32_t start, cnt;
 *   FaultRecorder_GetFaultWindow(&start, &cnt);
 *   for (uint32_t i = 0; i < cnt; i++) {
 *       uint32_t idx = (start + i) % RECORDER_TOTAL_FRAMES;
 *       FaultRecorder_ReadFrame(idx, spi_tx_buf);
 *       SPI4_SendFrame(spi_tx_buf, 400);  // 分包发送
 *   }
 */
void FaultRecorder_ReadFrame(uint32_t idx, void *dst)
{
    /* 环形index安全钳位 */
    uint32_t safe_idx = idx % g_recorder.capacity;                 // ← 防止越界
    memcpy(dst,                                                    // ← 目标：调用者缓冲区
           &g_recorder.base[safe_idx],                             // ← 来源：SDRAM 帧数据
           sizeof(RecorderFrame_t));                               // ← 400 字节
}
