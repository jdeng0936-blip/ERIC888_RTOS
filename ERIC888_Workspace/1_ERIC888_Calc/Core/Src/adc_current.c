#include "main.h"

/*
 * ✅ [Embedded-Engineer]
 * ADC1 + DMA2 循环扫描 6 路进线电流
 *
 * 工作原理：
 *   ADC1 连续扫描模式 + DMA2 Stream0 Circular，数组永远是"活的"。
 *   保护算法任务随时直接读，不需要中断，零 CPU 开销。
 *
 * ╔════════════════════════════════════════════════════════╗
 * ║ 通道映射 (已由用户确认 2026-03-14)                     ║
 * ╠═══════╦════════╦═══════════╦═══════════════════════════╣
 * ║ 索引  ║ 引脚   ║ ADC通道   ║ 物理含义                  ║
 * ╠═══════╬════════╬═══════════╬═══════════════════════════╣
 * ║ [0]   ║ PA1    ║ IN1       ║ 进线柜1 A相电流           ║
 * ║ [1]   ║ PA2    ║ IN2       ║ 进线柜1 B相电流           ║
 * ║ [2]   ║ PC0    ║ IN10      ║ 进线柜1 C相电流           ║
 * ║ [3]   ║ PC1    ║ IN11      ║ 进线柜2 A相电流           ║
 * ║ [4]   ║ PC2    ║ IN12      ║ 进线柜2 B相电流           ║
 * ║ [5]   ║ PC3    ║ IN13      ║ 进线柜2 C相电流           ║
 * ╚═══════╩════════╩═══════════╩═══════════════════════════╝
 *
 * 信号链路：CT互感器 → ACS725 霍尔传感器 → ADC
 */

#define ADC_CURRENT_CHANNELS 6

/* 通道索引宏定义 —— 让保护算法代码可读性更高 */
#define IDX_FEEDER1_IA  0   /* 进线柜1 A相 */
#define IDX_FEEDER1_IB  1   /* 进线柜1 B相 */
#define IDX_FEEDER1_IC  2   /* 进线柜1 C相 */
#define IDX_FEEDER2_IA  3   /* 进线柜2 A相 */
#define IDX_FEEDER2_IB  4   /* 进线柜2 B相 */
#define IDX_FEEDER2_IC  5   /* 进线柜2 C相 */

/* DMA 目标缓冲区 —— 保护算法直接读这个数组 */
volatile uint16_t adc_current_buf[ADC_CURRENT_CHANNELS] = {0};

/* HAL 句柄（需要保活，不能是局部变量） */
static ADC_HandleTypeDef hadc1;
static DMA_HandleTypeDef hdma_adc1;

void MX_ADC1_Current_Init(void)
{
    /* ── 1. 时钟使能 ─────────────────────── */
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* ── 2. GPIO 配置为模拟输入 ──────────── */
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;

    /* PA1, PA2 → 进线柜1 A/B相 */
    gpio.Pin = GPIO_PIN_1 | GPIO_PIN_2;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* PC0, PC1, PC2, PC3 → 进线柜1 C相 + 进线柜2 A/B/C相 */
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* ── 3. DMA2 Stream0 配置 ────────────── */
    hdma_adc1.Instance                 = DMA2_Stream0;
    hdma_adc1.Init.Channel             = DMA_CHANNEL_0;
    hdma_adc1.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode                = DMA_CIRCULAR;     /* 循环模式 */
    hdma_adc1.Init.Priority            = DMA_PRIORITY_LOW; /* 低优先级 */
    hdma_adc1.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_adc1);

    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

    /* ── 4. ADC1 配置 ────────────────────── */
    hadc1.Instance                   = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = ENABLE;
    hadc1.Init.ContinuousConvMode    = ENABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = ADC_CURRENT_CHANNELS;
    hadc1.Init.DMAContinuousRequests = ENABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SEQ_CONV;
    HAL_ADC_Init(&hadc1);

    /* ── 5. 通道配置 (Rank 1~6) ──────────── */
    ADC_ChannelConfTypeDef chcfg = {0};
    chcfg.SamplingTime = ADC_SAMPLETIME_84CYCLES;

    /* Rank 1: PA1 → 进线柜1 Ia */
    chcfg.Channel = ADC_CHANNEL_1;  chcfg.Rank = 1;
    HAL_ADC_ConfigChannel(&hadc1, &chcfg);

    /* Rank 2: PA2 → 进线柜1 Ib */
    chcfg.Channel = ADC_CHANNEL_2;  chcfg.Rank = 2;
    HAL_ADC_ConfigChannel(&hadc1, &chcfg);

    /* Rank 3: PC0 → 进线柜1 Ic */
    chcfg.Channel = ADC_CHANNEL_10; chcfg.Rank = 3;
    HAL_ADC_ConfigChannel(&hadc1, &chcfg);

    /* Rank 4: PC1 → 进线柜2 Ia */
    chcfg.Channel = ADC_CHANNEL_11; chcfg.Rank = 4;
    HAL_ADC_ConfigChannel(&hadc1, &chcfg);

    /* Rank 5: PC2 → 进线柜2 Ib */
    chcfg.Channel = ADC_CHANNEL_12; chcfg.Rank = 5;
    HAL_ADC_ConfigChannel(&hadc1, &chcfg);

    /* Rank 6: PC3 → 进线柜2 Ic */
    chcfg.Channel = ADC_CHANNEL_13; chcfg.Rank = 6;
    HAL_ADC_ConfigChannel(&hadc1, &chcfg);

    /* ── 6. 启动 ADC + DMA ───────────────── */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_current_buf, ADC_CURRENT_CHANNELS);
}
