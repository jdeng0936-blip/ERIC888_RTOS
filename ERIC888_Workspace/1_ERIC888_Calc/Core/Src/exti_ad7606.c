#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * ✅ [Embedded-Engineer]
 * 零抖动并发采样架构 (Ping-Pong Buffer)
 * 在 25.6kHz 时，每 39us 产生一次中断。中断内绝不能有任何耗时代码！
 *
 * ╔═══════════════════════════════════════════════════════════╗
 * ║ AD7606 8路电压通道映射 (已由用户确认 2026-03-14)          ║
 * ╠═══════╦═════════════╦══════════════════════════════════════╣
 * ║ 通道  ║ 位置        ║ 信号 (线电压)                        ║
 * ╠═══════╬═════════════╬══════════════════════════════════════╣
 * ║ ch[0] ║ 进线柜1     ║ Uab (A-B线电压)                     ║
 * ║ ch[1] ║ 进线柜1     ║ Ubc (B-C线电压)                     ║
 * ║ ch[2] ║ 进线柜2     ║ Uab (A-B线电压)                     ║
 * ║ ch[3] ║ 进线柜2     ║ Ubc (B-C线电压)                     ║
 * ║ ch[4] ║ 母线PT柜1   ║ Uab (A-B线电压)                     ║
 * ║ ch[5] ║ 母线PT柜1   ║ Ubc (B-C线电压)                     ║
 * ║ ch[6] ║ 母线PT柜2   ║ Uab (A-B线电压)                     ║
 * ║ ch[7] ║ 母线PT柜2   ║ Ubc (B-C线电压)                     ║
 * ╚═══════╩═════════════╩══════════════════════════════════════╝
 *
 * 4个测量位置 × 2个线电压 = 8路
 * 保护算法使用：进线电压 vs 母线电压 对比差分
 */

#define AD7606_BASE_ADDR ((uint32_t)0xD0000000)
#define POINTS_PER_BATCH 25 // 25.6kHz 下，25 个点约为 1ms 的数据量

typedef struct {
    int16_t ch[8][POINTS_PER_BATCH]; // 8通道，每通道25个样本
} AdcBatch_t;

typedef struct {
    AdcBatch_t buffer[2]; // Ping-Pong 缓冲区 [0] 和 [1]
    uint8_t write_idx;    // 当前 ISR 正在写的半区 (0 或 1)
    uint16_t sample_cnt;  // 当前半区写到了第几个样本 (0 ~ POINTS_PER_BATCH-1)
    volatile uint8_t read_idx; // 外部任务应该读哪个半区
} AdPingPongBuffer_t;

AdPingPongBuffer_t ad7606_pp_buf = {0};

/*
 * DSP 保护任务的句柄，由 fts_protect.c 创建后赋值。
 * EXTI1 中断通过 vTaskNotifyGiveFromISR() 来唤醒它。
 */
TaskHandle_t xFtsProtectTaskHandle = NULL;

/* EXTI1 中断处理函数 - 极速裸机，不经 HAL */
void EXTI1_IRQHandler_Fast(void)
{
    // 1. 清除中断标志位 (PB1)
    EXTI->PR = GPIO_PIN_1;

    // 2. 拿到目标填充地址
    uint8_t widx = ad7606_pp_buf.write_idx;
    uint16_t scnt = ad7606_pp_buf.sample_cnt;
    volatile int16_t *fmc_addr = (volatile int16_t *)AD7606_BASE_ADDR;

    // 3. 极致展开循环读取 8 通道
    ad7606_pp_buf.buffer[widx].ch[0][scnt] = *fmc_addr; /* 进线柜1 Uab */
    ad7606_pp_buf.buffer[widx].ch[1][scnt] = *fmc_addr; /* 进线柜1 Ubc */
    ad7606_pp_buf.buffer[widx].ch[2][scnt] = *fmc_addr; /* 进线柜2 Uab */
    ad7606_pp_buf.buffer[widx].ch[3][scnt] = *fmc_addr; /* 进线柜2 Ubc */
    ad7606_pp_buf.buffer[widx].ch[4][scnt] = *fmc_addr; /* 母线PT1 Uab */
    ad7606_pp_buf.buffer[widx].ch[5][scnt] = *fmc_addr; /* 母线PT1 Ubc */
    ad7606_pp_buf.buffer[widx].ch[6][scnt] = *fmc_addr; /* 母线PT2 Uab */
    ad7606_pp_buf.buffer[widx].ch[7][scnt] = *fmc_addr; /* 母线PT2 Ubc */

    // 4. 指针推移
    scnt++;
    if (scnt >= POINTS_PER_BATCH) {
        // 半区填满！切换缓冲区
        ad7606_pp_buf.sample_cnt = 0;
        ad7606_pp_buf.read_idx = widx;
        ad7606_pp_buf.write_idx = widx ^ 0x01;

        // 用 FreeRTOS TaskNotify 唤醒保护算法任务
        if (xFtsProtectTaskHandle != NULL) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            vTaskNotifyGiveFromISR(xFtsProtectTaskHandle, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    } else {
        ad7606_pp_buf.sample_cnt = scnt;
    }
}
