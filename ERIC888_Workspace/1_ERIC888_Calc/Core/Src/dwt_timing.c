#include "main.h"

/*
 * ✅ [Embedded-Engineer]
 * DWT (Data Watchpoint & Trace) 周期计数器
 * 用于精确测量 FTS 保护算法的实际执行时间。
 *
 * DWT->CYCCNT 是一个 32-bit 自由运行计数器，每个 CPU 周期 +1。
 * 180MHz 下：1 tick = 5.56ns，溢出周期 ~23.86 秒。
 *
 * 用法：
 *   DWT_StartMeasure(&ctx);
 *   ... 你要测的代码 ...
 *   DWT_StopMeasure(&ctx);
 *   // ctx.last_cycles 就是耗时周期数
 *   // ctx.last_us     就是耗时微秒数
 *   // ctx.max_us      是历史最大值
 */

typedef struct {
    uint32_t start_tick;
    uint32_t last_cycles;
    float    last_us;
    float    max_us;
    float    min_us;
    uint32_t count;       /* 测量次数 */
} DwtMeasure_t;

void DWT_Init(void)
{
    /* 使能 DWT 模块 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline void DWT_StartMeasure(DwtMeasure_t *ctx)
{
    ctx->start_tick = DWT->CYCCNT;
}

static inline void DWT_StopMeasure(DwtMeasure_t *ctx)
{
    uint32_t end = DWT->CYCCNT;
    ctx->last_cycles = end - ctx->start_tick; /* 自动处理溢出 */
    ctx->last_us = (float)ctx->last_cycles / (SystemCoreClock / 1000000U);
    ctx->count++;

    if (ctx->count == 1) {
        ctx->max_us = ctx->last_us;
        ctx->min_us = ctx->last_us;
    } else {
        if (ctx->last_us > ctx->max_us) ctx->max_us = ctx->last_us;
        if (ctx->last_us < ctx->min_us) ctx->min_us = ctx->last_us;
    }
}

/*
 * 全局测量上下文 —— 供 fts_protect.c 使用
 * 通过调试器或 SPI 数据包可以实时查看这些值。
 *
 * fts_measure_isr  → EXTI1 中断响应时间 (从进入中断到 FMC 读完)
 * fts_measure_algo → 保护算法执行时间 (dU/dt + 方向判断 + K-Switch)
 * fts_measure_e2e  → 端到端响应时间 (从 EXTI1 触发到 K-Switch 指令发出)
 */
DwtMeasure_t fts_measure_isr  = {0};
DwtMeasure_t fts_measure_algo = {0};
DwtMeasure_t fts_measure_e2e  = {0};
