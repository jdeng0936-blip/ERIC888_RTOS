/**
 * @file   bsp_kswitch_di.c
 * @brief  K-Switch 驱动/反馈/拓扑推断/策略路由 — 完整实现
 *
 * ✅ [Embedded-Engineer]
 * 本文件实现 4 大功能：
 *   ① GPIO 初始化 (DO 推挽输出 + DI 浮空输入)
 *   ② K-Switch 位置读取 (带消抖)
 *   ③ 运行拓扑自动推断 (DI + PT 交叉校验)
 *   ④ 切换策略路由表查询
 *
 * 引脚来源：888背板原理图20260311.pdf (唯一权威)
 */

#include "bsp_kswitch_di.h"

/* ═══════════ 全局变量 ═══════════ */

volatile Topology_t   g_topology    = TOPO_UNKNOWN;  /* 当前运行拓扑 */
volatile KSwitchPos_t g_kswitch_pos = {0, 0, 0, 0};  /* 当前 K-Switch 位置 */

/* ═══════════ 消抖缓存 (内部使用) ═══════════ */

/*
 * ✅ [Embedded-Engineer] 消抖原理：
 *   光耦输出存在毫秒级的上升/下降沿过渡，
 *   如果在过渡期间读取 GPIO 会得到不确定值。
 *   解决办法：连续读取 2 次 (间隔 ≥ 1ms)，
 *   两次结果一致才认定有效。
 *
 *   在 1ms 周期的 FTS_ProtectTask 中调用，
 *   天然满足 1ms 间隔，无需额外定时器。
 */
static KSwitchPos_t s_prev_pos = {0xFF, 0xFF, 0xFF, 0xFF}; /* 上一次读取值 */

/* ═══════════ GPIO 初始化 ═══════════ */

/**
 * @brief  初始化 K-Switch 驱动引脚 (DO 推挽输出)
 *
 * TRIGGER1~4 的 ON/OFF 引脚：
 *   K1: PI4(ON)  PI5(OFF)
 *   K2: PI6(ON)  PI7(OFF)
 *   K3: PE3(ON)  PI8(OFF)
 *   K4: PC13(ON) PI9(OFF)
 *
 * 初始状态：全部 LOW (不触发)
 */
void KSwitch_Drive_Init(void)
{
    /* 使能所有涉及的 GPIO 时钟 */
    __HAL_RCC_GPIOI_CLK_ENABLE();                                  /* PI4~PI9 */
    __HAL_RCC_GPIOE_CLK_ENABLE();                                  /* PE3 */
    __HAL_RCC_GPIOC_CLK_ENABLE();                                  /* PC13 */

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;                               /* 推挽输出 */
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;                              /* 高速 (微秒级响应) */

    /* PI4, PI5, PI6, PI7, PI8, PI9 */
    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 |
               GPIO_PIN_8 | GPIO_PIN_9;
    HAL_GPIO_Init(GPIOI, &gpio);

    /* PE3 */
    gpio.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOE, &gpio);

    /* PC13 */
    gpio.Pin = GPIO_PIN_13;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* 初始状态：全部 LOW */
    HAL_GPIO_WritePin(GPIOI, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 |
                              GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
}

/**
 * @brief  初始化 K-Switch 位置反馈引脚 (DI 浮空输入)
 *
 * POSITION1~4 的 ON/OFF 引脚：
 *   K1: PI1(ON)  PH15(OFF)
 *   K2: PH14(ON) PH13(OFF)
 *   K3: PA10(ON) PA12(OFF)
 *   K4: PI3(ON)  PI2(OFF)
 */
void KSwitch_DI_Init(void)
{
    /* 使能 GPIO 时钟 */
    __HAL_RCC_GPIOI_CLK_ENABLE();                                  /* PI1, PI2, PI3 */
    __HAL_RCC_GPIOH_CLK_ENABLE();                                  /* PH13, PH14, PH15 */
    __HAL_RCC_GPIOA_CLK_ENABLE();                                  /* PA10, PA12 */

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_INPUT;                                   /* 输入模式 */
    gpio.Pull  = GPIO_PULLDOWN;                                     /* 下拉：光耦不导通时为 LOW */
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    /* PI1, PI2, PI3 */
    gpio.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
    HAL_GPIO_Init(GPIOI, &gpio);

    /* PH13, PH14, PH15 */
    gpio.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOH, &gpio);

    /* PA10, PA12 */
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* 初始化消抖缓存 */
    s_prev_pos.k1 = 0xFF;
    s_prev_pos.k2 = 0xFF;
    s_prev_pos.k3 = 0xFF;
    s_prev_pos.k4 = 0xFF;
}

/* ═══════════ K-Switch 位置读取 (带消抖) ═══════════ */

/**
 * @brief  读取单路 K-Switch 的合分位
 *
 * 逻辑真值表 (ACPL247 光耦)：
 *   ON=HIGH, OFF=LOW  → 合位 (closed) → return 1
 *   ON=LOW,  OFF=HIGH → 分位 (open)   → return 0
 *   ON=LOW,  OFF=LOW  → 异常           → return 0xFF
 *   ON=HIGH, OFF=HIGH → 异常           → return 0xFF
 */
static uint8_t Read_Single_KSwitch(GPIO_TypeDef *on_port, uint16_t on_pin,
                                   GPIO_TypeDef *off_port, uint16_t off_pin)
{
    uint8_t on_state  = (HAL_GPIO_ReadPin(on_port,  on_pin)  == GPIO_PIN_SET); /* 合位信号 */
    uint8_t off_state = (HAL_GPIO_ReadPin(off_port, off_pin) == GPIO_PIN_SET); /* 分位信号 */

    if (on_state && !off_state) {
        return 1;                                                   /* ← 合位 */
    } else if (!on_state && off_state) {
        return 0;                                                   /* ← 分位 */
    } else {
        return 0xFF;                                                /* ← 异常 (断线或粘死) */
    }
}

/**
 * @brief  读取全部 K-Switch 位置 (带 1ms 消抖)
 *
 * ✅ [Embedded-Engineer] 消抖策略：
 *   每次调用读取一次 GPIO 原始值，
 *   只有与上一次 (s_prev_pos) 一致时才更新 g_kswitch_pos。
 *   由于在 1ms 周期 Task 中调用，天然实现了 1ms 间隔消抖。
 */
KSwitchPos_t KSwitch_ReadPosition(void)
{
    KSwitchPos_t raw;

    raw.k1 = Read_Single_KSwitch(KFB_K1_ON_PORT, KFB_K1_ON_PIN,
                                  KFB_K1_OFF_PORT, KFB_K1_OFF_PIN);
    raw.k2 = Read_Single_KSwitch(KFB_K2_ON_PORT, KFB_K2_ON_PIN,
                                  KFB_K2_OFF_PORT, KFB_K2_OFF_PIN);
    raw.k3 = Read_Single_KSwitch(KFB_K3_ON_PORT, KFB_K3_ON_PIN,
                                  KFB_K3_OFF_PORT, KFB_K3_OFF_PIN);
    raw.k4 = Read_Single_KSwitch(KFB_K4_ON_PORT, KFB_K4_ON_PIN,
                                  KFB_K4_OFF_PORT, KFB_K4_OFF_PIN);

    /* 消抖：与上次一致才认定有效 */
    if (raw.k1 == s_prev_pos.k1) g_kswitch_pos.k1 = raw.k1;
    if (raw.k2 == s_prev_pos.k2) g_kswitch_pos.k2 = raw.k2;
    if (raw.k3 == s_prev_pos.k3) g_kswitch_pos.k3 = raw.k3;
    if (raw.k4 == s_prev_pos.k4) g_kswitch_pos.k4 = raw.k4;

    s_prev_pos = raw;                                               /* 保存本次原始值 */

    return g_kswitch_pos;
}

/* ═══════════ 拓扑推断 ═══════════ */

/**
 * @brief  根据 DI 位置 + PT 电压推断当前运行拓扑
 *
 * ✅ [Embedded-Engineer] 修正后推断矩阵：
 *
 * 物理拓扑：
 *   电源1 ── K1 ──┤母线段1├── K2+K4(母联) ──┤母线段2├── K3 ── 电源2
 *
 * ┌─────┬─────┬─────┬─────┬──────────┬──────────┬──────────────────┐
 * │ K1  │ K2  │ K3  │ K4  │ 母线PT1  │ 母线PT2  │ → 拓扑            │
 * ├─────┼─────┼─────┼─────┼──────────┼──────────┼──────────────────┤
 * │ 合  │ 合  │ 分  │ 合  │ 有压     │ 有压     │ TOPO_SPLIT_SRC1  │
 * │ 分  │ 合  │ 合  │ 合  │ 有压     │ 有压     │ TOPO_SPLIT_SRC2  │
 * │ 合  │ 分  │ 合  │ 分  │ 有压     │ 有压     │ TOPO_PARALLEL    │
 * │ 分  │ 分  │ 分  │ 分  │ 无压     │ 无压     │ TOPO_MAINTENANCE │
 * │ DI/PT 不一致 或 K2/K4 不同步       │          │ TOPO_ERROR       │
 * └─────┴─────┴─────┴─────┴──────────┴──────────┴──────────────────┘
 *
 * @param  pos              K-Switch 位置状态 (含 K4)
 * @param  bus1_has_voltage  母线段1 PT 是否有压 (>10% 额定值)
 * @param  bus2_has_voltage  母线段2 PT 是否有压
 * @return 推断出的运行拓扑
 */
Topology_t KSwitch_DetectTopology(const KSwitchPos_t *pos,
                                   uint8_t bus1_has_voltage,
                                   uint8_t bus2_has_voltage)
{
    /* 如果任何 DI 读取异常 (0xFF)，直接报错 */
    if (pos->k1 == 0xFF || pos->k2 == 0xFF ||
        pos->k3 == 0xFF || pos->k4 == 0xFF) {
        return TOPO_ERROR;                                          /* ← 辅助触点断线 */
    }

    /*
     * ✅ [Embedded-Engineer] K2/K4 同步校验：
     *   K4 是母联辅助开关，必须与 K2 同合同分。
     *   如果 K2/K4 状态不一致，说明联动回路异常，直接报错。
     */
    if (pos->k2 != pos->k4) {
        return TOPO_ERROR;                                          /* ← K2/K4 不同步！ */
    }

    /* ── 分段运行-1 (电源1带全所)：K1合 K2合 K3分 K4合 ── */
    if (pos->k1 == 1 && pos->k2 == 1 && pos->k3 == 0 && pos->k4 == 1) {
        /* 交叉校验：电源1经K2贯通两段母线，两段都应有压 */
        if (bus1_has_voltage && bus2_has_voltage) {
            return TOPO_SPLIT_SRC1;                                 /* ✅ 电源1经母联带全所 */
        } else {
            return TOPO_ERROR;                                      /* ⚠️ K2合但某段PT无压 */
        }
    }

    /* ── 分段运行-2 (电源2带全所)：K1分 K2合 K3合 K4合 ── */
    if (pos->k1 == 0 && pos->k2 == 1 && pos->k3 == 1 && pos->k4 == 1) {
        if (bus1_has_voltage && bus2_has_voltage) {
            return TOPO_SPLIT_SRC2;                                 /* ✅ 电源2经母联带全所 */
        } else {
            return TOPO_ERROR;
        }
    }

    /* ── 并列运行 (双电源各供各段)：K1合 K2分 K3合 K4分 ── */
    if (pos->k1 == 1 && pos->k2 == 0 && pos->k3 == 1 && pos->k4 == 0) {
        if (bus1_has_voltage && bus2_has_voltage) {
            return TOPO_PARALLEL;                                   /* ✅ 各供各段 */
        } else {
            return TOPO_ERROR;                                      /* ⚠️ 双合但PT无压 */
        }
    }

    /* ── 检修方式：全部分开 ── */
    if (pos->k1 == 0 && pos->k2 == 0 && pos->k3 == 0 && pos->k4 == 0) {
        return TOPO_MAINTENANCE;                                    /* ✅ 全停 */
    }

    /* ── 其他未预期组合 ── */
    return TOPO_ERROR;                                              /* ← 非法拓扑 */
}

/* ═══════════ 切换策略路由表 ═══════════ */

/*
 * ✅ [Embedded-Engineer] 策略路由表 — 核心中的核心！
 *
 * 使用 FaultType_t 的枚举值 (定义在 fts_protect.c 中)：
 *   FAULT_FEEDER1 = 1, FAULT_FEEDER2 = 2, FAULT_BUS = 3
 *
 * 修正后路由矩阵 (K2=母联, K4 联动K2)：
 *
 * ┌──────────────┬──────────────┬──────────┬────────────┬────────┐
 * │ 拓扑          │ 故障源       │ 先断      │ 后合        │ 需同期 │
 * ├──────────────┼──────────────┼──────────┼────────────┼────────┤
 * │ PARALLEL     │ FEEDER1      │ K1       │ K2+K4(母联) │ 是     │
 * │ PARALLEL     │ FEEDER2      │ K3       │ K2+K4(母联) │ 是     │
 * │ PARALLEL     │ BUS          │ 全闭锁    │ —          │ —      │
 * │ SPLIT_SRC1   │ FEEDER1      │ K1       │ K3          │ 是     │
 * │ SPLIT_SRC1   │ BUS          │ 全闭锁    │ —          │ —      │
 * │ SPLIT_SRC2   │ FEEDER2      │ K3       │ K1          │ 是     │
 * │ SPLIT_SRC2   │ BUS          │ 全闭锁    │ —          │ —      │
 * │ 其他         │ 任何          │ 全闭锁    │ —          │ —      │
 * └──────────────┴──────────────┴──────────┴────────────┴────────┘
 *
 * ⚠️ 注意：
 *   ACT_CLOSE_K2 会自动同时合 K4 (在 KSwitch_ExecuteMake 中联动)。
 *   ACT_TRIP_K2  会自动同时跳 K4 (在 KSwitch_ExecuteBreak 中联动)。
 */

/* 故障类型前向声明 (枚举值必须与 fts_protect.c 中一致) */
#define FAULT_TYPE_FEEDER1  1
#define FAULT_TYPE_FEEDER2  2
#define FAULT_TYPE_BUS      3

/**
 * @brief  根据当前拓扑和故障类型查询切换策略
 * @param  topo        当前运行拓扑
 * @param  fault_type  故障类型 (FAULT_FEEDER1/2/BUS 的数值)
 * @return 切换策略 (先断+后合+是否需同期)
 */
SwitchStrategy_t KSwitch_LookupStrategy(Topology_t topo, uint8_t fault_type)
{
    SwitchStrategy_t s = { ACT_LOCKOUT_ALL, ACT_NONE, 0 };         /* 默认：全闭锁 */

    switch (topo) {

    /* ══════ 并列运行 (K1合 K2分 K3合 K4分) ══════ */
    case TOPO_PARALLEL:
        if (fault_type == FAULT_TYPE_FEEDER1) {
            /*
             * 进线1故障 → 跳K1 → 合K2+K4(母联)
             * 物理意义：隔离故障电源1，合母联让电源2经母线2→K2→母线1 供全所
             */
            s.break_action = ACT_TRIP_K1;
            s.make_action  = ACT_CLOSE_K2;                         /* ← 自动联动 K4 */
            s.need_sync    = 1;                                     /* 合母联前必须同期 */
        }
        else if (fault_type == FAULT_TYPE_FEEDER2) {
            /*
             * 进线2故障 → 跳K3 → 合K2+K4(母联)
             * 物理意义：隔离故障电源2，合母联让电源1经母线1→K2→母线2 供全所
             */
            s.break_action = ACT_TRIP_K3;
            s.make_action  = ACT_CLOSE_K2;
            s.need_sync    = 1;
        }
        /* BUS 故障 → 默认全闭锁 */
        break;

    /* ══════ 分段运行-1 (K1合 K2合 K3分 K4合, 电源1带全所) ══════ */
    case TOPO_SPLIT_SRC1:
        if (fault_type == FAULT_TYPE_FEEDER1) {
            /*
             * 电源1故障 → 跳K1 → 合K3(投入电源2)
             * 物理意义：K2 保持合不动，合K3让电源2经进线2→母线2→K2→母线1 供全所
             * 切换后系统变为 SPLIT_SRC2 运行方式
             */
            s.break_action = ACT_TRIP_K1;
            s.make_action  = ACT_CLOSE_K3;                         /* K2保持合 */
            s.need_sync    = 1;                                     /* 必须同期 */
        }
        /* FEEDER2 故障：K3 已分，电源2 未并网，不影响当前运行 → 不动作 */
        /* BUS 故障 → 默认全闭锁 */
        break;

    /* ══════ 分段运行-2 (K1分 K2合 K3合 K4合, 电源2带全所) ══════ */
    case TOPO_SPLIT_SRC2:
        if (fault_type == FAULT_TYPE_FEEDER2) {
            /*
             * 电源2故障 → 跳K3 → 合K1(投入电源1)
             * 物理意义：K2 保持合不动，合K1让电源1经进线1→母线1→K2→母线2 供全所
             * 切换后系统变为 SPLIT_SRC1 运行方式
             */
            s.break_action = ACT_TRIP_K3;
            s.make_action  = ACT_CLOSE_K1;
            s.need_sync    = 1;
        }
        /* FEEDER1 故障：K1 已分，电源1 未并网，不影响 → 不动作 */
        /* BUS 故障 → 默认全闭锁 */
        break;

    /* ══════ 检修/异常/未知 → 全闭锁 ══════ */
    case TOPO_MAINTENANCE:
    case TOPO_ERROR:
    case TOPO_UNKNOWN:
    default:
        /* 保持默认：ACT_LOCKOUT_ALL */
        break;
    }

    return s;
}

/* ═══════════ K-Switch 执行动作 ═══════════ */

/**
 * @brief  发送脉冲到指定 K-Switch 引脚 (合闸或跳闸)
 *
 * 脉冲宽度约 5μs @180MHz (200 次空循环)
 */
void KSwitch_Pulse_ON(GPIO_TypeDef *port, uint16_t pin)
{
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);                     /* ← 拉高光耦 */
    for (volatile int i = 0; i < 200; i++);                        /* ← ~5μs 脉宽 */
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);                  /* ← 释放 */
}

void KSwitch_Pulse_OFF(GPIO_TypeDef *port, uint16_t pin)
{
    KSwitch_Pulse_ON(port, pin);                                    /* 逻辑相同，都是脉冲触发 */
}

/**
 * @brief  执行先断 (Break) 动作
 */
void KSwitch_ExecuteBreak(SwitchAction_t act)
{
    switch (act) {
    case ACT_TRIP_K1:
        KSwitch_Pulse_ON(KDRV_K1_OFF_PORT, KDRV_K1_OFF_PIN);      /* ← 跳K1 */
        break;
    case ACT_TRIP_K2:
        /* K2+K4 联动跳闸 (母联 + 辅助母联同步跳开) */
        KSwitch_Pulse_ON(KDRV_K2_OFF_PORT, KDRV_K2_OFF_PIN);      /* ← 跳K2(母联) */
        KSwitch_Pulse_ON(KDRV_K4_OFF_PORT, KDRV_K4_OFF_PIN);      /* ← 跳K4(辅助母联联动) */
        break;
    case ACT_TRIP_K3:
        KSwitch_Pulse_ON(KDRV_K3_OFF_PORT, KDRV_K3_OFF_PIN);      /* ← 跳K3 */
        break;
    case ACT_LOCKOUT_ALL:
        /* 全闭锁：K1+K2+K3+K4 全部跳开 */
        KSwitch_Pulse_ON(KDRV_K1_OFF_PORT, KDRV_K1_OFF_PIN);
        KSwitch_Pulse_ON(KDRV_K2_OFF_PORT, KDRV_K2_OFF_PIN);
        KSwitch_Pulse_ON(KDRV_K3_OFF_PORT, KDRV_K3_OFF_PIN);
        KSwitch_Pulse_ON(KDRV_K4_OFF_PORT, KDRV_K4_OFF_PIN);      /* ← K4也必须跳 */
        break;
    default:
        break;
    }
}

/**
 * @brief  执行后合 (Make) 动作
 */
void KSwitch_ExecuteMake(SwitchAction_t act)
{
    switch (act) {
    case ACT_CLOSE_K1:
        KSwitch_Pulse_ON(KDRV_K1_ON_PORT, KDRV_K1_ON_PIN);        /* ← 合K1 */
        break;
    case ACT_CLOSE_K2:
        /* K2+K4 联动合闸 (母联 + 辅助母联同步合上) */
        KSwitch_Pulse_ON(KDRV_K2_ON_PORT, KDRV_K2_ON_PIN);        /* ← 合K2(母联) */
        KSwitch_Pulse_ON(KDRV_K4_ON_PORT, KDRV_K4_ON_PIN);        /* ← 合K4(辅助母联联动) */
        break;
    case ACT_CLOSE_K3:
        KSwitch_Pulse_ON(KDRV_K3_ON_PORT, KDRV_K3_ON_PIN);        /* ← 合K3 */
        break;
    default:
        break;                                                      /* ACT_NONE / ACT_LOCKOUT_ALL */
    }
}
