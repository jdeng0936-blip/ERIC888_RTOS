/**
 * @file   bsp_kswitch_di.h
 * @brief  K-Switch 驱动 (DO) + 位置反馈 (DI) + 拓扑推断模块
 *
 * ✅ [Embedded-Engineer]
 * 引脚映射严格以 888背板原理图20260311.pdf 为唯一权威来源。
 *
 * ┌──────────────────────────────────────────────────────────────┐
 * │ 信号链 (驱动输出 DO)：                                        │
 * │   MCU GPIO → ACPL247 光耦 → QX3120/TLP250 → KNX42120 MOSFET │
 * │                                                              │
 * │ 信号链 (位置反馈 DI)：                                        │
 * │   K-Switch 辅助触点 → ACPL247 光耦 → MCU GPIO (浮空输入)     │
 * └──────────────────────────────────────────────────────────────┘
 *
 * 物理拓扑示意：
 *
 *   电源1 ─┤K1├─ 母线段1 ─┤K2(母联)├─ 母线段2 ─┤K3├─ 电源2
 *                                                     K4 = 备用
 */
#ifndef __BSP_KSWITCH_DI_H__
#define __BSP_KSWITCH_DI_H__

#include "main.h"
#include <stdint.h>
#include "eric888_spi_protocol.h"  /* ← Topology_t, SwitchAction_t 等共享定义 */

/* ═══════════════════════════════════════════════════════════════
 * K-Switch 驱动引脚 (TRIGGER DO 输出)
 * 来源：888背板原理图 P1 / P11 TRIGGER 网络名
 *
 * │ 通道 │ ON 引脚  │ OFF 引脚 │ 网络名            │
 * │ K1   │ PI4      │ PI5      │ TRIGGER1_ON/OFF   │
 * │ K2   │ PI6      │ PI7      │ TRIGGER2_ON/OFF   │
 * │ K3   │ PE3      │ PI8      │ TRIGGER3_ON/OFF   │
 * │ K4   │ PC13     │ PI9      │ TRIGGER4_ON/OFF   │
 * ═══════════════════════════════════════════════════════════════ */
#define KDRV_K1_ON_PORT    GPIOI
#define KDRV_K1_ON_PIN     GPIO_PIN_4     /* PI4  = TRIGGER1_ON  */
#define KDRV_K1_OFF_PORT   GPIOI
#define KDRV_K1_OFF_PIN    GPIO_PIN_5     /* PI5  = TRIGGER1_OFF */

#define KDRV_K2_ON_PORT    GPIOI          /* 母联合闸 */
#define KDRV_K2_ON_PIN     GPIO_PIN_6     /* PI6  = TRIGGER2_ON  */
#define KDRV_K2_OFF_PORT   GPIOI          /* 母联跳闸 */
#define KDRV_K2_OFF_PIN    GPIO_PIN_7     /* PI7  = TRIGGER2_OFF */

#define KDRV_K3_ON_PORT    GPIOE
#define KDRV_K3_ON_PIN     GPIO_PIN_3     /* PE3  = TRIGGER3_ON  */
#define KDRV_K3_OFF_PORT   GPIOI
#define KDRV_K3_OFF_PIN    GPIO_PIN_8     /* PI8  = TRIGGER3_OFF */

#define KDRV_K4_ON_PORT    GPIOC
#define KDRV_K4_ON_PIN     GPIO_PIN_13    /* PC13 = TRIGGER4_ON  */
#define KDRV_K4_OFF_PORT   GPIOI
#define KDRV_K4_OFF_PIN    GPIO_PIN_9     /* PI9  = TRIGGER4_OFF */

/* ═══════════════════════════════════════════════════════════════
 * K-Switch 位置反馈引脚 (POSITION DI 输入)
 * 来源：888背板原理图 P1 POSITION 网络名 (CON1 连接器)
 *
 * │ 通道 │ ON (合位) │ OFF (分位) │ 网络名                │
 * │ K1   │ PI1       │ PH15       │ POSITION1_ON/OFF     │
 * │ K2   │ PH14      │ PH13       │ POSITION2_ON/OFF     │
 * │ K3   │ PA10      │ PA12       │ POSITION3_ON/OFF     │
 * │ K4   │ PI3       │ PI2        │ POSITION4_ON/OFF     │
 * ═══════════════════════════════════════════════════════════════ */
#define KFB_K1_ON_PORT     GPIOI
#define KFB_K1_ON_PIN      GPIO_PIN_1     /* PI1  = POSITION1_ON  (K1合位) */
#define KFB_K1_OFF_PORT    GPIOH
#define KFB_K1_OFF_PIN     GPIO_PIN_15    /* PH15 = POSITION1_OFF (K1分位) */

#define KFB_K2_ON_PORT     GPIOH
#define KFB_K2_ON_PIN      GPIO_PIN_14    /* PH14 = POSITION2_ON  (K2合位) */
#define KFB_K2_OFF_PORT    GPIOH
#define KFB_K2_OFF_PIN     GPIO_PIN_13    /* PH13 = POSITION2_OFF (K2分位) */

#define KFB_K3_ON_PORT     GPIOA
#define KFB_K3_ON_PIN      GPIO_PIN_10    /* PA10 = POSITION3_ON  (K3合位) */
#define KFB_K3_OFF_PORT    GPIOA
#define KFB_K3_OFF_PIN     GPIO_PIN_12    /* PA12 = POSITION3_OFF (K3分位) */

#define KFB_K4_ON_PORT     GPIOI
#define KFB_K4_ON_PIN      GPIO_PIN_3     /* PI3  = POSITION4_ON  (K4合位) */
#define KFB_K4_OFF_PORT    GPIOI
#define KFB_K4_OFF_PIN     GPIO_PIN_2     /* PI2  = POSITION4_OFF (K4分位) */

/* ═══════════ 位置状态结构体 ═══════════ */

/**
 * K-Switch 合分位状态
 *   1 = 合位 (closed)
 *   0 = 分位 (open)
 *   0xFF = 异常 (ON/OFF 都不亮 = 辅助触点断线)
 */
typedef struct {
    uint8_t k1;         /* K1 进线柜1电源 */
    uint8_t k2;         /* K2 母联开关 */
    uint8_t k3;         /* K3 进线柜2电源 */
    uint8_t k4;         /* K4 备用 */
} KSwitchPos_t;

/* ═══════════ 运行拓扑枚举 ═══════════ */
/*
 * Topology_t 和 SwitchAction_t 已在 eric888_spi_protocol.h 中统一定义，
 * A/B 板共享同一份枚举，避免失同步。此处不再重复定义。
 */


/**
 * 切换策略条目
 * 由拓扑+故障类型查表得到
 */
typedef struct {
    SwitchAction_t break_action;   /* 先断 (必须第一个执行) */
    SwitchAction_t make_action;    /* 后合 (过零确认后执行) */
    uint8_t        need_sync;      /* 是否需要 SPLL 同期检查 */
} SwitchStrategy_t;

/* ═══════════ 导出全局变量 ═══════════ */

extern volatile Topology_t   g_topology;
extern volatile KSwitchPos_t g_kswitch_pos;

/* ═══════════ 外部接口 ═══════════ */

void            KSwitch_DI_Init(void);
void            KSwitch_Drive_Init(void);
KSwitchPos_t    KSwitch_ReadPosition(void);

Topology_t      KSwitch_DetectTopology(const KSwitchPos_t *pos,
                                        uint8_t bus1_has_voltage,
                                        uint8_t bus2_has_voltage);

SwitchStrategy_t KSwitch_LookupStrategy(Topology_t topo, uint8_t fault_type);

void            KSwitch_Pulse_ON(GPIO_TypeDef *port, uint16_t pin);
void            KSwitch_Pulse_OFF(GPIO_TypeDef *port, uint16_t pin);
void            KSwitch_ExecuteBreak(SwitchAction_t act);
void            KSwitch_ExecuteMake(SwitchAction_t act);

#endif /* __BSP_KSWITCH_DI_H__ */
