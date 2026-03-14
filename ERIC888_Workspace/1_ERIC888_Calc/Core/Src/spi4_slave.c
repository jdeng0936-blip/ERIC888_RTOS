#include "main.h"
#include "eric888_spi_protocol.h"
#include "FreeRTOS.h"
#include "task.h"
#include "fault_recorder.h"         /* ← FaultRecorder_IsFrozen() */

/*
 * ✅ [Embedded-Engineer]
 * SPI4 Slave 驱动 — A板(Slave) ↔ B板(Master)
 *
 * 硬件连接 (RS-422 差分隔离)：
 *   PE2 = SPI4_SCK
 *   PE4 = SPI4_NSS (硬件)
 *   PE5 = SPI4_MISO (A→B, Slave发出)
 *   PE6 = SPI4_MOSI (B→A, Slave接收)
 *   PI11 = IRQ 握手线 (A板→B板，数据就绪通知)
 *
 * 工作模式：
 *   B板(Master) 周期性发 CMD_READ_STATUS 请求
 *   A板(Slave) 收到后打包保护状态回传
 */

/* ═══════════ HAL 句柄 ═══════════ */
static SPI_HandleTypeDef hspi4;

/* ═══════════ 收发缓冲区 ═══════════ */
static Eric888_SPI_Frame spi4_rx_frame;
static Eric888_SPI_Frame spi4_tx_frame;
static uint8_t spi4_seq = 0;

/* ═══════════ 外部状态引用 ═══════════ */

/*
 * FtsState_t, FaultType_t, Topology_t, FTS_StatusSnapshot_t
 * 已在 eric888_spi_protocol.h 中统一定义 (由 #include 链引入)。
 * 此处仅声明 fts_protect.c 中导出的全局变量。
 */

/* 导出的保护状态 (fts_protect.c) */
extern volatile FtsState_t  g_fts_state;
extern volatile FaultType_t g_fault_type;
extern volatile uint8_t     g_backup_available;   /* 备用电源可用标志 */
extern volatile int16_t     g_phase_diff_deg10;   /* 主备相角差 (×10, 0.1°) */

/* 导出的运行拓扑 (bsp_kswitch_di.c) */
extern volatile Topology_t  g_topology;

/* DWT 计时 (dwt_timing.c) */
typedef struct {
    uint32_t start_tick;
    uint32_t last_cycles;
    float    last_us;
    float    max_us;
    float    min_us;
    uint32_t count;
} DwtMeasure_t;
extern DwtMeasure_t fts_measure_algo;

/* 电流缓冲区 (adc_current.c) */
extern volatile uint16_t adc_current_buf[6];

static void SPI4_BuildStatusResponse(void)
{
    FTS_StatusSnapshot_t snap = {0};

    /*
     * ✅ [Embedded-Engineer] 安全审计修复：临界区防撕裂
     *
     * 保护任务 (优先级 2) 可随时抢占 SPI4 ISR 回调 (优先级 6)。
     * 如果在逐字段读取全局 volatile 变量的过程中被抢占，
     * snap 中可能出现前半截旧值 + 后半截新值的矛盾数据。
     *
     * 用 taskENTER_CRITICAL() 关调度器+中断，保证原子读取。
     * 临界区内仅做赋值操作（~100ns），远小于 1ms 保护周期。
     */
    taskENTER_CRITICAL();
    snap.fts_state          = g_fts_state;
    snap.fault_type         = g_fault_type;
    snap.topology           = g_topology;
    snap.backup_available   = g_backup_available;
    snap.phase_diff_deg10   = g_phase_diff_deg10;
    snap.recorder_frozen    = FaultRecorder_IsFrozen();
    taskEXIT_CRITICAL();

    /* DWT 计时数据 (非安全关键，无需临界区) */
    snap.algo_max_us  = (uint16_t)(fts_measure_algo.max_us * 10.0f);
    snap.algo_last_us = (uint16_t)(fts_measure_algo.last_us * 10.0f);
    snap.algo_count   = fts_measure_algo.count;

    /* 6路电流实时值 */
    snap.feeder1_ia = adc_current_buf[0];
    snap.feeder1_ib = adc_current_buf[1];
    snap.feeder1_ic = adc_current_buf[2];
    snap.feeder2_ia = adc_current_buf[3];
    snap.feeder2_ib = adc_current_buf[4];
    snap.feeder2_ic = adc_current_buf[5];

    /* 系统运行时间 */
    snap.uptime_ms = HAL_GetTick();

    /* 打包成 SPI 协议帧 */
    Eric888_InitFrame(&spi4_tx_frame, CMD_READ_STATUS, spi4_seq++);
    spi4_tx_frame.len = sizeof(FTS_StatusSnapshot_t);
    memcpy(spi4_tx_frame.payload, &snap, sizeof(FTS_StatusSnapshot_t));
    Eric888_SealFrame(&spi4_tx_frame);
}

static void SPI4_ProcessRxFrame(void)
{
    /* 校验帧 */
    if (Eric888_ValidateFrame(&spi4_rx_frame) != 0) {
        return; /* CRC 错误或帧头不对，丢弃 */
    }

    switch (spi4_rx_frame.cmd) {
    case CMD_READ_STATUS:
        SPI4_BuildStatusResponse();
        /* 启动下一轮 SPI 传输 (Slave 模式，等待 Master 时钟) */
        HAL_SPI_TransmitReceive_IT(&hspi4,
            (uint8_t *)&spi4_tx_frame, (uint8_t *)&spi4_rx_frame,
            sizeof(Eric888_SPI_Frame));
        break;

    case CMD_HEARTBEAT:
        /* B板心跳，回 ACK */
        Eric888_InitFrame(&spi4_tx_frame, CMD_ACK, spi4_seq++);
        Eric888_SealFrame(&spi4_tx_frame);
        HAL_SPI_TransmitReceive_IT(&hspi4,
            (uint8_t *)&spi4_tx_frame, (uint8_t *)&spi4_rx_frame,
            sizeof(Eric888_SPI_Frame));
        break;

    default:
        /* 未知命令，回 NACK */
        Eric888_InitFrame(&spi4_tx_frame, CMD_NACK, spi4_seq++);
        Eric888_SealFrame(&spi4_tx_frame);
        HAL_SPI_TransmitReceive_IT(&hspi4,
            (uint8_t *)&spi4_tx_frame, (uint8_t *)&spi4_rx_frame,
            sizeof(Eric888_SPI_Frame));
        break;
    }
}

/* ═══════════ HAL 回调 ═══════════ */

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI4) {
        /* 收发完成，处理接收到的帧 */
        SPI4_ProcessRxFrame();
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI4) {
        /* SPI 错误，重新启动监听 */
        HAL_SPI_TransmitReceive_IT(&hspi4,
            (uint8_t *)&spi4_tx_frame, (uint8_t *)&spi4_rx_frame,
            sizeof(Eric888_SPI_Frame));
    }
}

/* ═══════════ IRQ 握手线 ═══════════ */

/*
 * A板拉高 PI11 → 通知 B板 "有新数据可读"
 * B板看到中断后发 CMD_READ_STATUS
 */
static void SPI4_AssertIRQ(void)
{
    HAL_GPIO_WritePin(GPIOI, GPIO_PIN_11, GPIO_PIN_SET);
}

static void SPI4_DeassertIRQ(void)
{
    HAL_GPIO_WritePin(GPIOI, GPIO_PIN_11, GPIO_PIN_RESET);
}

/* ═══════════ 对外接口 ═══════════ */

void SPI4_Slave_Init(void)
{
    /* ── 1. 时钟使能 ── */
    __HAL_RCC_SPI4_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    /* ── 2. SPI4 GPIO (PE2/PE4/PE5/PE6) ── */
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI4;

    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_5 | GPIO_PIN_6; /* SCK/MISO/MOSI */
    HAL_GPIO_Init(GPIOE, &gpio);

    /* NSS = 硬件管理 (Slave 模式) */
    gpio.Pin = GPIO_PIN_4; /* NSS */
    HAL_GPIO_Init(GPIOE, &gpio);

    /* ── 3. PI11 = IRQ 输出 (推挽) ── */
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin   = GPIO_PIN_11;
    HAL_GPIO_Init(GPIOI, &gpio);
    SPI4_DeassertIRQ();

    /* ── 4. SPI4 配置 (Slave, 8-bit, 全双工) ── */
    hspi4.Instance               = SPI4;
    hspi4.Init.Mode              = SPI_MODE_SLAVE;
    hspi4.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi4.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi4.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi4.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi4.Init.NSS               = SPI_NSS_HARD_INPUT;
    hspi4.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi4.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi4.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    HAL_SPI_Init(&hspi4);

    /* ── 5. 使能 SPI4 中断 ── */
    HAL_NVIC_SetPriority(SPI4_IRQn, 6, 0); /* 低于保护任务 */
    HAL_NVIC_EnableIRQ(SPI4_IRQn);

    /* ── 6. 预装默认状态应答，开始监听 ── */
    SPI4_BuildStatusResponse();
    HAL_SPI_TransmitReceive_IT(&hspi4,
        (uint8_t *)&spi4_tx_frame, (uint8_t *)&spi4_rx_frame,
        sizeof(Eric888_SPI_Frame));

    /* ── 7. 拉高 IRQ 通知 B板 "A板就绪" ── */
    SPI4_AssertIRQ();
}

/* SPI4 全局中断入口 (需在 stm32f4xx_it.c 中转发) */
void SPI4_IRQHandler(void)
{
    HAL_SPI_IRQHandler(&hspi4);
}
