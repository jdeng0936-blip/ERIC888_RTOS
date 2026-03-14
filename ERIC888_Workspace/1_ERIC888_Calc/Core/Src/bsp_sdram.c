/**
 * @file   bsp_sdram.c
 * @brief  外部 SDRAM 驱动 — W9825G6KH / IS42S16160J (32MB, FMC Bank2)
 *
 * ✅ [Embedded-Engineer]
 *
 * 硬件参数：
 *   型号：W9825G6KH / IS42S16160J
 *   容量：4 Banks × 4M × 16-bit = 32 MB
 *   映射：FMC SDRAM Bank2 → 0xC0000000
 *   数据位宽：16-bit
 *   行地址：13 位 (A0-A12)
 *   列地址：9 位 (A0-A8)
 *
 * FMC 引脚分配 (STM32F429 标准设计)：
 *   地址线  A0-A12 : PF0-PF5, PF12-PF15, PG0-PG1, PG2 (13根)
 *   数据线  D0-D15 : PD14-PD15, PD0-PD1, PE7-PE15, PD8-PD10
 *   Bank    BA0,BA1: PG4, PG5
 *   控制线  SDNWE  : PH5 (或 PC0)
 *           SDNCAS : PG15
 *           SDNRAS : PF11
 *           SDCKE1 : PB5 (或 PH7)
 *           SDNE1  : PB6 (或 PH6)
 *           SDCLK  : PG8
 *
 * ⚠️ 上电时序极其严苛：
 *   JEDEC 标准要求 SDRAM 必须按固定序列初始化，否则内部状态机混乱！
 */
#include "bsp_sdram.h"
#include <string.h>

/* ═══════════ 私有句柄 ═══════════ */
static SDRAM_HandleTypeDef hsdram;

/* ═══════════ FMC GPIO 初始化 ═══════════ */

/**
 * ✅ [Embedded-Engineer] FMC 引脚全量配置
 *
 * 所有 SDRAM 相关引脚必须配置为：
 *   - 模式：复用推挽 (AF_PP)
 *   - 速度：Very High (100MHz FMC 时钟要求)
 *   - 复用：AF12 (FMC)
 *   - 上下拉：无
 */
static void BSP_SDRAM_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_FMC;

    /* 使能所有涉及的 GPIO 时钟 */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    /*
     * ── GPIOB ──
     * PB5 = FMC_SDCKE1  (Bank2 时钟使能)
     * PB6 = FMC_SDNE1   (Bank2 片选)
     */
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6;
    HAL_GPIO_Init(GPIOB, &gpio);

    /*
     * ── GPIOD ──
     * PD0  = FMC_D2      PD1  = FMC_D3
     * PD8  = FMC_D13     PD9  = FMC_D14     PD10 = FMC_D15
     * PD14 = FMC_D0      PD15 = FMC_D1
     */
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 |
               GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gpio);

    /*
     * ── GPIOE ──
     * PE0  = FMC_NBL0    PE1  = FMC_NBL1   (字节访问掩码，16-bit 模式需要)
     * PE7  = FMC_D4      PE8  = FMC_D5      PE9  = FMC_D6      PE10 = FMC_D7
     * PE11 = FMC_D8      PE12 = FMC_D9      PE13 = FMC_D10     PE14 = FMC_D11
     * PE15 = FMC_D12
     */
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 |
               GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
               GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gpio);

    /*
     * ── GPIOF ──
     * PF0-PF5   = FMC_A0-A5
     * PF11      = FMC_SDNRAS  (行地址选通)
     * PF12-PF15 = FMC_A6-A9
     */
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
               GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 |
               GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &gpio);

    /*
     * ── GPIOG ──
     * PG0  = FMC_A10     PG1  = FMC_A11     PG2 = FMC_A12     (13位行地址完成)
     * PG4  = FMC_BA0     PG5  = FMC_BA1     (2位 Bank 地址)
     * PG8  = FMC_SDCLK   (SDRAM 主时钟)
     * PG15 = FMC_SDNCAS  (列地址选通)
     */
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 |
               GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOG, &gpio);

    /*
     * ── GPIOH ──
     * PH5 = FMC_SDNWE   (写使能)
     */
    gpio.Pin = GPIO_PIN_5;
    HAL_GPIO_Init(GPIOH, &gpio);
}

/* ═══════════ SDRAM 上电初始化序列 ═══════════ */

/**
 * ✅ [Embedded-Engineer] 严苛的 JEDEC 上电序列
 *
 * SDRAM 内部是一个状态机。上电后处于未知态，
 * 必须严格按以下顺序发送命令才能进入可用状态：
 *
 *   Step 1: Clock Enable     — 启动 SDRAM 内部时钟分配网络
 *   Step 2: Delay ≥ 100μs    — 等待内部 PLL 稳定 (JEDEC 强制要求)
 *   Step 3: PALL             — 全部预充电，关闭所有打开的 Bank/Row
 *   Step 4: Auto-Refresh ×8  — 连续 8 次自刷新，稳定内部电容
 *   Step 5: Load Mode Reg    — 设置 Burst Length、CAS Latency 等
 *   Step 6: 设定刷新定时器   — FMC_SDRTR 寄存器，维持数据不丢失
 *
 * ⚠️ 跳过任何一步或顺序错误 = SDRAM 内部状态机紊乱 = 随机数据 = HardFault！
 */
static void BSP_SDRAM_InitSequence(void)
{
    FMC_SDRAM_CommandTypeDef cmd = {0};

    /*
     * Step 1: 时钟使能 (Clock Enable)
     *
     * 物理意义：SDRAM 是同步器件，所有操作都依赖时钟。
     * 上电后时钟未分配，芯片处于"睡眠"状态。
     * 此命令把 FMC_SDCLK 连接到 SDRAM 内部。
     */
    cmd.CommandMode            = FMC_SDRAM_CMD_CLK_ENABLE;
    cmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK2;
    cmd.AutoRefreshNumber      = 1;
    cmd.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(&hsdram, &cmd, 0xFFFF);

    /*
     * Step 2: 延时 100 微秒以上
     *
     * 物理意义：SDRAM 内部锁相环 (DLL) 需要时间锁定到 SDCLK 频率。
     * JEDEC 标准规定至少等待 100μs。我们用 HAL_Delay(1) = 1ms，
     * 远超要求，确保万无一失。
     */
    HAL_Delay(1);

    /*
     * Step 3: 全部预充电 (PALL - Precharge All)
     *
     * 物理意义：SDRAM 内部有 4 个 Bank，每个 Bank 可能有一个
     * 处于"打开"状态的行。PALL 强制关闭所有 Bank 的活动行，
     * 让 SDRAM 回到一个干净的已知状态。
     * 类比：关闭所有打开的文件，准备重新开始。
     */
    cmd.CommandMode            = FMC_SDRAM_CMD_PALL;
    cmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK2;
    cmd.AutoRefreshNumber      = 1;
    cmd.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(&hsdram, &cmd, 0xFFFF);

    /*
     * Step 4: 自动刷新 8 次 (Auto-Refresh × 8)
     *
     * 物理意义：SDRAM 的存储单元是微型电容，电荷会自然泄漏。
     * 上电后内部电容处于随机电荷状态。
     * 连续 8 次自动刷新循环遍历所有行，将存储阵列稳定到一致状态。
     * JEDEC 规定至少 2 次，实际工程惯例用 8 次以提高可靠性。
     */
    cmd.CommandMode            = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    cmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK2;
    cmd.AutoRefreshNumber      = 8;  /* ← 连续 8 次！ */
    cmd.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(&hsdram, &cmd, 0xFFFF);

    /*
     * Step 5: 加载模式寄存器 (Load Mode Register)
     *
     * 物理意义：告诉 SDRAM 如何工作。模式寄存器位定义：
     *   Bit [2:0] = Burst Length    = 000  → 1 (单次突发)
     *   Bit [3]   = Burst Type      = 0    → Sequential
     *   Bit [6:4] = CAS Latency     = 011  → 3 个时钟周期
     *   Bit [8:7] = Operating Mode  = 00   → Standard
     *   Bit [9]   = Write Burst     = 1    → Single location access
     *
     * CAS Latency = 3 的含义：
     *   从发出列地址到数据出现在数据线上，需要 3 个 SDCLK 周期。
     *   这是安全的折中值 — CAS=2 更快但在高频下可能不稳定。
     */
    #define SDRAM_MODEREG_BURST_LENGTH_1      ((uint16_t)0x0000)
    #define SDRAM_MODEREG_BURST_TYPE_SEQ      ((uint16_t)0x0000)
    #define SDRAM_MODEREG_CAS_LATENCY_3       ((uint16_t)0x0030)
    #define SDRAM_MODEREG_OPERATING_STD       ((uint16_t)0x0000)
    #define SDRAM_MODEREG_WRITEBURST_SINGLE   ((uint16_t)0x0200)

    cmd.CommandMode            = FMC_SDRAM_CMD_LOAD_MODE;
    cmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK2;
    cmd.AutoRefreshNumber      = 1;
    cmd.ModeRegisterDefinition = SDRAM_MODEREG_BURST_LENGTH_1
                               | SDRAM_MODEREG_BURST_TYPE_SEQ
                               | SDRAM_MODEREG_CAS_LATENCY_3
                               | SDRAM_MODEREG_OPERATING_STD
                               | SDRAM_MODEREG_WRITEBURST_SINGLE;
    HAL_SDRAM_SendCommand(&hsdram, &cmd, 0xFFFF);

    /*
     * Step 6: 设定刷新定时器
     *
     * 物理意义：SDRAM 必须周期性刷新以保持数据。W9825G6KH 要求
     * 8192 行在 64ms 内全部刷新 → 每行刷新间隔 = 64ms / 8192 = 7.8μs。
     *
     * 刷新计数值 = (SDRAM时钟频率 × 刷新间隔) - 20
     *            = (90MHz × 7.8μs) - 20
     *            = 702 - 20 = 682
     *
     * 注：减去 20 是安全余量，防止刷新来不及导致数据丢失。
     */
    HAL_SDRAM_ProgramRefreshRate(&hsdram, 682);
}

/* ═══════════ 对外接口 ═══════════ */

void BSP_SDRAM_Init(void)
{
    FMC_SDRAM_TimingTypeDef timing = {0};

    /* 使能 FMC 时钟 */
    __HAL_RCC_FMC_CLK_ENABLE();

    /* 初始化所有 FMC GPIO */
    BSP_SDRAM_GPIO_Init();

    /* ── FMC SDRAM 控制寄存器 (SDCR) 配置 ── */
    hsdram.Instance                = FMC_SDRAM_DEVICE;
    hsdram.Init.SDBank             = FMC_SDRAM_BANK2;          /* Bank2 → 0xC0000000 */
    hsdram.Init.ColumnBitsNumber   = FMC_SDRAM_COLUMN_BITS_NUM_9;   /* 9位列地址 (512 列) */
    hsdram.Init.RowBitsNumber      = FMC_SDRAM_ROW_BITS_NUM_13;     /* 13位行地址 (8192 行) */
    hsdram.Init.MemoryDataWidth    = FMC_SDRAM_MEM_BUS_WIDTH_16;    /* 16-bit 数据总线 */
    hsdram.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;  /* 4 个内部 Bank */
    hsdram.Init.CASLatency         = FMC_SDRAM_CAS_LATENCY_3;      /* CAS = 3 (匹配模式寄存器) */
    hsdram.Init.WriteProtection    = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    hsdram.Init.SDClockPeriod      = FMC_SDRAM_CLOCK_PERIOD_2;     /* SDCLK = HCLK/2 = 90MHz */
    hsdram.Init.ReadBurst          = FMC_SDRAM_RBURST_ENABLE;      /* 突发读使能 (提高效率) */
    hsdram.Init.ReadPipeDelay      = FMC_SDRAM_RPIPE_DELAY_1;      /* 1 HCLK 读管线延迟 */

    /*
     * ── FMC SDRAM 时序寄存器 (SDTR) 配置 ──
     *
     * 所有时序参数单位为 SDCLK 周期 (= HCLK/2)。
     * 按 W9825G6KH 数据手册 (-6 速度等级) 取值。
     */
    timing.LoadToActiveDelay    = 2;  /* tMRD: 模式寄存器设置到激活 = 2 CLK */
    timing.ExitSelfRefreshDelay = 7;  /* tXSR: 退出自刷新到激活 = 72ns → 7 CLK */
    timing.SelfRefreshTime      = 4;  /* tRAS: 自刷新时间 = 42ns → 4 CLK */
    timing.RowCycleDelay        = 7;  /* tRC:  行周期 = 63ns → 6~7 CLK */
    timing.WriteRecoveryTime    = 2;  /* tWR:  写恢复 = 2 CLK (auto precharge) */
    timing.RPDelay              = 2;  /* tRP:  预充电到激活 = 18ns → 2 CLK */
    timing.RCDDelay             = 2;  /* tRCD: 行激活到列命令 = 18ns → 2 CLK */

    /* 执行 HAL SDRAM 初始化 (配置 SDCR + SDTR 寄存器) */
    HAL_SDRAM_Init(&hsdram, &timing);

    /* 执行 JEDEC 强制上电序列 */
    BSP_SDRAM_InitSequence();
}

void BSP_SDRAM_Test(void)
{
    /*
     * ✅ [Embedded-Engineer] 硬件防御自检
     *
     * 在 SDRAM 起始地址写入魔法数字并回读。
     * 如果不一致，说明：
     *   - SDRAM 芯片故障 / 虚焊
     *   - FMC 配置错误 (Bank/时序/引脚)
     *   - 供电不稳
     *
     * 此时绝对禁止进入 RTOS 调度！死循环报错。
     * 运维人员可通过 SWD 调试器看到 PC 停在这里。
     */
    volatile uint32_t *p = (volatile uint32_t *)SDRAM_BASE_ADDRESS;

    /* 写入多个位置验证地址线和数据线完整性 */
    p[0] = SDRAM_TEST_MAGIC;                  /* 起始地址 */
    p[1] = ~SDRAM_TEST_MAGIC;                 /* 取反验证所有数据线 */
    p[SDRAM_SIZE_BYTES / 4 - 1] = 0xDEADBEEF; /* 末尾地址验证高位地址线 */

    /* 读回校验 */
    if (p[0] != SDRAM_TEST_MAGIC) {
        while (1) { __NOP(); }  /* ← 死循环！SDRAM 起始地址读写失败 */
    }
    if (p[1] != ~SDRAM_TEST_MAGIC) {
        while (1) { __NOP(); }  /* ← 死循环！数据线错误 */
    }
    if (p[SDRAM_SIZE_BYTES / 4 - 1] != 0xDEADBEEF) {
        while (1) { __NOP(); }  /* ← 死循环！高位地址线错误 (地址折叠) */
    }
}
