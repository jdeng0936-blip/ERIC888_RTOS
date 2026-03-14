#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "arm_math.h"         /* ← CMSIS-DSP 硬件加速 */
#include "fault_recorder.h"   /* ← SDRAM 故障录波 */
#include "bsp_kswitch_di.h"   /* ← K-Switch 驱动/反馈/拓扑推断/策略路由 */

/* ═══════════════════════════════════════════════════════════════════════
 * @file   fts_protect.c
 * @brief  FTS 快切保护核心算法模块 (四大引擎已全部集成)
 *
 * 引擎清单：
 *   ① 1ms 极速方向保护引擎  — dU/dt + IIR 滤波 P=ui 极性路由
 *   ② 相量追踪 SPLL 引擎    — 线电压专供版 Clarke→Park→PI 锁相环
 *   ③ 防桥连换流引擎        — ADC 闭环过零 + 10ms DWT 硬件超时
 *   ④ 稳态状态机管家        — NORMAL→DIP→TRIPPED/SYNC_WAIT/LOCKOUT
 *
 * 系统拓扑 (双进线 + 双母线)：
 *
 *   电源1 ──┤进线柜1├── K1 ──┤母线PT1├── 负荷A
 *   电源2 ──┤进线柜2├── K3 ──┤母线PT2├── 负荷B
 *                    (母联开关可能连接两段母线)
 *
 * 采样架构：
 *   AD7606 (25.6kHz) → 8路线电压 (4位置 × Uab/Ubc)
 *   内部ADC (DMA)    → 6路电流 (2进线柜 × Ia/Ib/Ic via ACS725)
 * ═══════════════════════════════════════════════════════════════════════ */

/* ═══════════ DWT 计时 (dwt_timing.c) ═══════════ */

typedef struct {
    uint32_t start_tick;
    uint32_t last_cycles;
    float    last_us;
    float    max_us;
    float    min_us;
    uint32_t count;
} DwtMeasure_t;
extern DwtMeasure_t fts_measure_algo;
static inline void DWT_StartMeasure(DwtMeasure_t *c) { c->start_tick = DWT->CYCCNT; }  // ← 锁存当前 DWT 周期计数器
static inline void DWT_StopMeasure(DwtMeasure_t *c) {
    uint32_t e = DWT->CYCCNT;                                     // ← 读取结束时刻
    c->last_cycles = e - c->start_tick;                           // ← 差值即为本次算法消耗的时钟周期数
    c->last_us = (float)c->last_cycles / (SystemCoreClock / 1000000U); // ← 转换为微秒
    c->count++;
    if (c->count == 1) { c->max_us = c->last_us; c->min_us = c->last_us; }
    else { if (c->last_us > c->max_us) c->max_us = c->last_us; if (c->last_us < c->min_us) c->min_us = c->last_us; }
}

/* ═══════════ 外部引用 ═══════════ */

#define POINTS_PER_BATCH 25       // ← 每个 Ping-Pong 批次的采样点数 (25.6kHz / 25 ≈ 1ms 一批)

typedef struct {
    int16_t ch[8][POINTS_PER_BATCH];  // ← 8路 AD7606 通道，每通道 25 个采样点
} AdcBatch_t;

typedef struct {
    AdcBatch_t buffer[2];             // ← Ping-Pong 双缓冲区
    uint8_t write_idx;
    uint16_t sample_cnt;
    volatile uint8_t read_idx;
} AdPingPongBuffer_t;

extern AdPingPongBuffer_t ad7606_pp_buf;   // ← 由 exti_ad7606.c 管理的全局缓冲
extern TaskHandle_t xFtsProtectTaskHandle; // ← 本任务句柄，供 ISR 发通知
extern volatile uint16_t adc_current_buf[6]; // ← 内部 ADC DMA 循环缓冲区 (6路电流最新值)

/* ═══════════ AD7606 电压通道索引 ═══════════ */
/*
 * 物理接线映射 (来自背板原理图 888_20260311.pdf 核实)：
 *   ch[0] = V1 = 进线柜1 Uab    ch[1] = V2 = 进线柜1 Ubc
 *   ch[2] = V3 = 进线柜2 Uab    ch[3] = V4 = 进线柜2 Ubc
 *   ch[4] = V5 = 母线PT1 Uab    ch[5] = V6 = 母线PT1 Ubc
 *   ch[6] = V7 = 母线PT2 Uab    ch[7] = V8 = 母线PT2 Ubc
 */
#define V_FEEDER1_UAB   0
#define V_FEEDER1_UBC   1
#define V_FEEDER2_UAB   2
#define V_FEEDER2_UBC   3
#define V_BUS_PT1_UAB   4
#define V_BUS_PT1_UBC   5
#define V_BUS_PT2_UAB   6
#define V_BUS_PT2_UBC   7

/* ═══════════ ADC 电流通道索引 ═══════════ */
/*
 * 内部 ADC1 DMA 循环采样 (ACS725 霍尔传感器)：
 *   [0] = 进线柜1 Ia   [1] = 进线柜1 Ib   [2] = 进线柜1 Ic
 *   [3] = 进线柜2 Ia   [4] = 进线柜2 Ib   [5] = 进线柜2 Ic
 */
#define IDX_FEEDER1_IA  0
#define IDX_FEEDER1_IB  1
#define IDX_FEEDER1_IC  2
#define IDX_FEEDER2_IA  3
#define IDX_FEEDER2_IB  4
#define IDX_FEEDER2_IC  5

/* ═══════════ 保护参数 ═══════════ */

#define VOLTAGE_DIP_THRESHOLD   3277  // ← 满量程 10% (±10V 16bit: 1LSB ≈ 0.305mV)
#define VOLTAGE_DIFF_THRESHOLD  1000  // ← 进线 vs 母线电压差异判定阈值
#define CURRENT_ZERO_OFFSET     2048  // ← ACS725 零电流中点 (12-bit ADC: 3.3V/2 ≈ 2048)
#define CURRENT_OVERCURRENT     500   // ← 过流门槛 (需现场标定)
#define TRIP_CONFIRM_COUNT      2     // ← 连续 N 批确认才跳闸 (防单脉冲误动)

/* ═══════════ K-Switch 引脚定义已移入 bsp_kswitch_di.h ═══════════ */
/*
 * ✅ [原理图确认] 888背板原理图20260311.pdf
 *
 * TRIGGER (DO 驱动输出)：                   POSITION (DI 反馈输入)：
 *   K1: PI4(ON)  PI5(OFF)                 K1: PI1(合)   PH15(分)
 *   K2: PI6(ON)  PI7(OFF)  [母联]        K2: PH14(合)  PH13(分)
 *   K3: PE3(ON)  PI8(OFF)                 K3: PA10(合)  PA12(分)
 *   K4: PC13(ON) PI9(OFF)                 K4: PI3(合)   PI2(分)
 *
 * 具体宏定义见 bsp_kswitch_di.h (KDRV_Kx_ON/OFF + KFB_Kx_ON/OFF)
 */

/* ═══════════ 状态机枚举 ═══════════ */

/*
 * FtsState_t, FaultType_t, Topology_t, SwitchAction_t
 * 已在 eric888_spi_protocol.h 中统一定义 (通过 bsp_kswitch_di.h 链式引入)。
 * A/B 板共享同一份定义，禁止在此重复声明！
 */

/* 导出供 SPI4 状态快照读取 */
volatile FtsState_t  g_fts_state = FTS_STATE_INIT;
volatile FaultType_t g_fault_type = FAULT_NONE;
volatile uint8_t     g_backup_available = 0;  /* 备用电源可用标志 (0=未知/不可用, 1=可用) */
volatile int16_t     g_phase_diff_deg10 = 0;  /* 主备相角差 (×10, 单位 0.1°, 由 SPLL 周期性更新) */

/* ═══════════════════════════════════════════════════════════════════════
 * 引擎②：线电压专供版 SPLL 软件锁相环
 *
 * ⚠️ 物理约束 (雷区2)：
 *   AD7606 采集的是线电压 Uab/Ubc，不是相电压 Va/Vb/Vc！
 *   如果直接用标准 Clarke 变换 (假定相电压)，锁出来的相位会永远偏 30°！
 *   合闸瞬间两个电源相位差 30° = 炸机级冲击电流！
 *
 * 修正公式 (线电压 → αβ 正交坐标)：
 *   Vα = Uab
 *   Vβ = (Uab + 2·Ubc) / √3
 *
 * 信号流向：
 *   Uab,Ubc → [线电压Clarke] → Vα,Vβ → [Park变换] → Vd,Vq
 *                                                      ↓
 *   θ ← [积分器] ← ω ← [PI控制器] ← (0 - Vq)
 * ═══════════════════════════════════════════════════════════════════════ */

#define INV_SQRT3       0.577350269f  // ← 1/√3，线电压 Clarke 变换系数
#define OMEGA_NOMINAL   314.159265f   // ← 2π×50Hz = 314.16 rad/s (50Hz 电网额定角频率)
#define TWO_PI          6.283185307f  // ← 2π
#define TS_SEC          0.001f        // ← SPLL 更新周期 = 1ms (和保护任务同频)
#define SYNC_ANGLE_FAST 0.35f         // ← 20° 弧度阈值：小于此值允许极速盲切
#define SYNC_ANGLE_OK   0.087f        // ← 5°  弧度阈值：小于此值执行同期捕捉合闸
#define SYNC_TIMEOUT_MS 2000          // ← 同期捕捉超时 2 秒 (残压衰减上限)

/*
 * ✅ [Embedded-Engineer]
 * SPLL 句柄结构体 (每路电源各一个实例)
 */
typedef struct {
    float32_t theta;     // ← 当前锁定相位角 [0, 2π)
    float32_t omega;     // ← 当前锁定角频率 (rad/s)
    float32_t vd;        // ← Park 变换后的 d 轴电压 (幅值信息)
    float32_t vq;        // ← Park 变换后的 q 轴电压 (偏差信息，PI 驱动此趋近 0)
    arm_pid_instance_f32 pi; // ← CMSIS-DSP 硬件加速 PID 控制器
} SPLL_Handle_t;

/* 主电源 SPLL 和备用电源 SPLL 各一个实例 */
static SPLL_Handle_t spll_main;     // ← 跟踪进线柜1 (电源1) 的相位
static SPLL_Handle_t spll_backup;   // ← 跟踪进线柜2 (电源2) 的相位

/*
 * ✅ [Embedded-Engineer]
 * SPLL 初始化 — 设置 PI 参数和初始状态
 *
 * PI 参数说明 (系统实操型取值)：
 *   Kp = 28.0  → 快响应，100ms 内锁住一个从 0 开始的 50Hz 信号
 *   Ki = 600.0 → 消除稳态频偏，大约 0.5s 收敛到 ±0.1Hz 以内
 *   这组参数针对 10kV 配电网的频率波动范围 (49.5~50.5Hz) 做过整定
 */
static void SPLL_Init(SPLL_Handle_t *pll)
{
    pll->theta = 0.0f;             // ← 初始相位归零
    pll->omega = OMEGA_NOMINAL;    // ← 初始角频率设为额定 50Hz
    pll->vd    = 0.0f;
    pll->vq    = 0.0f;

    /* CMSIS-DSP PID 初始化 */
    pll->pi.Kp = 28.0f;           // ← 比例增益：决定了锁相速度
    pll->pi.Ki = 600.0f;          // ← 积分增益：消除频率稳态偏差
    pll->pi.Kd = 0.0f;            // ← 微分增益：SPLL 不需要 D 项
    arm_pid_init_f32(&pll->pi, 1); // ← 1 = 复位积分器状态
}

/*
 * ✅ [Embedded-Engineer]
 * SPLL 线电压专供版更新函数 — 每 1ms 调用一次
 *
 * 参数说明：
 *   pll  — 锁相环句柄 (主电源或备用电源)
 *   Uab  — 当前 Uab 线电压瞬时采样值 (float32_t)
 *   Ubc  — 当前 Ubc 线电压瞬时采样值 (float32_t)
 *
 * 内部计算流程：
 *   ① Clarke(线电压版) → ② Park → ③ PI(err = 0 - Vq) → ④ 频率积分 → ⑤ θ 输出
 */
static void SPLL_Update_Line2Line(SPLL_Handle_t *pll, float32_t Uab, float32_t Ubc)
{
    float32_t valpha, vbeta;        // ← Clarke 变换输出 (正交坐标系)
    float32_t sin_theta, cos_theta; // ← 当前锁定角的三角函数值

    /* ① 线电压专制版 Clarke 变换 (不是标准相电压版！) */
    valpha = Uab;                                      // ← Vα 直接等于 Uab
    vbeta  = (Uab + 2.0f * Ubc) * INV_SQRT3;          // ← Vβ = (Uab + 2*Ubc) / √3

    /* ② Park 变换：αβ → dq (将旋转坐标系对齐到电网基波) */
    sin_theta = arm_sin_f32(pll->theta);               // ← CMSIS-DSP 硬件查表正弦
    cos_theta = arm_cos_f32(pll->theta);               // ← CMSIS-DSP 硬件查表余弦
    pll->vd =  valpha * cos_theta + vbeta * sin_theta; // ← d 轴 = 电压幅值
    pll->vq = -valpha * sin_theta + vbeta * cos_theta; // ← q 轴 = 相位偏差 (PI 驱动此→0)

    /* ③ PI 控制器闭环：目标是让 Vq = 0 (即锁定相位) */
    float32_t pi_out = arm_pid_f32(&pll->pi, 0.0f - pll->vq); // ← 硬件乘加加速

    /* ④ 更新角频率和相位角 */
    pll->omega = OMEGA_NOMINAL + pi_out;               // ← 额定频率 + PI 修正量
    pll->theta += pll->omega * TS_SEC;                 // ← 欧拉积分更新相位

    /* ⑤ 相位角归一化到 [0, 2π) */
    if (pll->theta >= TWO_PI) pll->theta -= TWO_PI;
    else if (pll->theta < 0.0f) pll->theta += TWO_PI;
}

/*
 * ✅ [Embedded-Engineer]
 * 获取主备电源相角差 Δθ (弧度)
 * 返回值范围: [-π, +π]
 *   正值 = 备用电源超前主电源
 *   负值 = 备用电源滞后主电源
 */
static float32_t SPLL_Get_Phase_Difference(void)
{
    float32_t diff = spll_backup.theta - spll_main.theta; // ← 备用 - 主电源
    if (diff > 3.14159265f)  diff -= TWO_PI;              // ← 归一化到 [-π, π]
    if (diff < -3.14159265f) diff += TWO_PI;

    /* 同步更新全局变量供 SPI4 快照读取 (弧度 → 度×10) */
    g_phase_diff_deg10 = (int16_t)(diff * (1800.0f / 3.14159265f));

    return diff;
}

/* ═══════════════════════════════════════════════════════════════════════
 * 引擎①：1ms 极速方向保护 (dU/dt + IIR 滤波 P=ui 极性路由)
 * ═══════════════════════════════════════════════════════════════════════ */

/*
 * 计算一个位置两个线电压的突变量 (取 Uab 和 Ubc 中较大的 |Δu|)
 */
static int32_t Calc_Position_Transient(const AdcBatch_t *curr, const AdcBatch_t *prev,
                                       int ch_uab, int ch_ubc)
{
    int32_t d_uab = (int32_t)curr->ch[ch_uab][POINTS_PER_BATCH - 1]
                  - (int32_t)prev->ch[ch_uab][POINTS_PER_BATCH - 1];
    int32_t d_ubc = (int32_t)curr->ch[ch_ubc][POINTS_PER_BATCH - 1]
                  - (int32_t)prev->ch[ch_ubc][POINTS_PER_BATCH - 1];
    if (d_uab < 0) d_uab = -d_uab;
    if (d_ubc < 0) d_ubc = -d_ubc;
    return (d_uab > d_ubc) ? d_uab : d_ubc;
}

/*
 * 获取一个位置当前线电压的绝对幅值 (取最新样本点的值)
 */
static int32_t Get_Position_Voltage(const AdcBatch_t *batch, int ch_uab, int ch_ubc)
{
    int32_t v_uab = (int32_t)batch->ch[ch_uab][POINTS_PER_BATCH - 1];
    int32_t v_ubc = (int32_t)batch->ch[ch_ubc][POINTS_PER_BATCH - 1];
    if (v_uab < 0) v_uab = -v_uab;
    if (v_ubc < 0) v_ubc = -v_ubc;
    return (v_uab + v_ubc) / 2;
}

/* 电流幅值计算 (三相绝对值之和，用于过流粗判) */
static int32_t Calc_Feeder_Current_Magnitude(uint16_t ia, uint16_t ib, uint16_t ic)
{
    int32_t da = (int32_t)ia - CURRENT_ZERO_OFFSET; if (da < 0) da = -da;
    int32_t db = (int32_t)ib - CURRENT_ZERO_OFFSET; if (db < 0) db = -db;
    int32_t dc = (int32_t)ic - CURRENT_ZERO_OFFSET; if (dc < 0) dc = -dc;
    return da + db + dc;
}

/* ── 瞬态功率方向判定参数 ── */
#define PWR_DIR_THRESHOLD      500000.0f  // ← 功率积分判定门槛
#define DI_DT_THRESHOLD        50.0f     // ← 电流变化率判定门槛
#define PT_BREAK_DI_CEILING    15.0f     // ← PT 断线判定：ΔI 绝对值低于此值 = 电流无波动
#define IIR_ALPHA              0.8f      // ← IIR 低通滤波器系数 (旧值权重)
#define IIR_BETA               0.2f      // ← IIR 低通滤波器系数 (新值权重，α+β=1)

/*
 * ✅ [Embedded-Engineer] 极速瞬态方向判别核心引擎 (1ms 硬实时内执行)
 *
 * 计算公式：
 *   i_filtered = 0.8 × i_filtered_prev + 0.2 × i_raw   (一阶 IIR 低通滤波)
 *   P_sum = u_inst × i_filtered                          (瞬时功率)
 *   ΔI   = i_filtered - i_prev                           (滤波后的电流变化率)
 *
 * ⚠️ 雷区1 修复：
 *   原先直接用 i_raw - i_prev 计算 di/dt，在 25.6kHz 高频下 ADC 的 1 个 LSB 抖动
 *   都会被放大成巨大的 ΔI。变频器启动毛刺就能让装置瞬间死锁。
 *   现在加了一阶 IIR 低通（时间常数 ≈ 5个采样周期 ≈ 195μs），
 *   既保留了故障暂态的陡峭度，又滤掉了 ADC 底噪和 PWM 谐波毛刺。
 *
 * 极性矩阵路由 (Polarity Matrix Routing)：
 *   内部故障 (母线短路): 电网向母线猛灌电流 → P >> 0, ΔI >> 0
 *   外部故障 (系统短路): 电动机反向倒送电   → P << 0, ΔI << 0
 */
static FaultType_t Calc_Directional_Power(const AdcBatch_t *v_buf, int ch_v, int ch_i)
{
    /* 静态 IIR 滤波缓存 (每个电流通道独立保存) */
    static float32_t i_filtered_cache[6] = {0}; // ← 上一次滤波后的电流值
    static float32_t i_prev_cache[6] = {0};     // ← 上一次用于 di/dt 的滤波值

    /* 读取当前瞬时值 */
    int32_t u_inst = v_buf->ch[ch_v][POINTS_PER_BATCH - 1];  // ← AD7606 最新电压采样
    float32_t i_raw = (float32_t)adc_current_buf[ch_i] - CURRENT_ZERO_OFFSET; // ← 内部 ADC 电流 (减去零点偏移)

    /* ── 一阶 IIR 低通滤波 (雷区1 核心修复) ── */
    i_filtered_cache[ch_i] = IIR_ALPHA * i_filtered_cache[ch_i]  // ← 80% 旧值 (惯性)
                           + IIR_BETA  * i_raw;                  // ← 20% 新值 (跟踪)

    /* 计算瞬时功率 P = u × i_filtered */
    float32_t P_sum = (float32_t)u_inst * i_filtered_cache[ch_i]; // ← 功率极性 = 能量流向

    /* 计算滤波后的电流变化率 ΔI = i_filtered(n) - i_filtered(n-1) */
    float32_t di_sum = i_filtered_cache[ch_i] - i_prev_cache[ch_i]; // ← 变化率方向 = 故障注入方向
    i_prev_cache[ch_i] = i_filtered_cache[ch_i];                    // ← 保存本次值供下次计算

    /* ── 极性矩阵路由判断 ── */
    if (P_sum < -PWR_DIR_THRESHOLD && di_sum < -DI_DT_THRESHOLD) {
        return FAULT_FEEDER1; // ← 外部故障：功率反向倒送 + 电流反向跃变 → 允许快切
    }
    else if (P_sum > PWR_DIR_THRESHOLD && di_sum > DI_DT_THRESHOLD) {
        return FAULT_BUS;     // ← 内部短路：功率正向猛灌 + 电流正向剧增 → 绝对闭锁
    }

    return FAULT_NONE;        // ← 功率极性不明确，保守不动作
}

/*
 * ✅ [Embedded-Engineer]
 * 引擎⑤：PT 断线防误动检测
 *
 * 物理原理 (这是保护工程师的常识)：
 *
 *   场景A — 真正的电网短路/跌落：
 *     电压骤降 (有 ΔU) + 电流剧变 (有 ΔI，过流或反向)
 *     → 这是真故障，允许保护动作
 *
 *   场景B — PT 保险丝熔断 / PT 二次接线松动：
 *     电压突降到 0 (有 ΔU) + 电流纹丝不动 (无 ΔI，无过流)
 *     → 这不是真故障！是测量回路坏了！
 *     → 如果此时误切换，会把正常运行的负载切断 = 重大安全生产事故！
 *
 * 判定逻辑：
 *   if (|ΔU| > 阈值  &&  |ΔI| < PT_BREAK_DI_CEILING  &&  I < 过流阈值)
 *       → FAULT_PT_BROKEN → 闭锁报警，绝对禁止切换！
 *
 * 信号流向：
 *   AD7606 ΔU ──→ ┌──────────────────┐
 *                  │ PT 断线判定矩阵    │──→ FAULT_PT_BROKEN
 *   ADC ΔI ──────→ │  有ΔU 无ΔI 无过流  │
 *                  └──────────────────┘
 */
static uint8_t Check_PT_Broken(int ch_i_a, int ch_i_b, int ch_i_c)
{
    /*
     * 复用 Calc_Directional_Power 内部的 IIR 滤波缓存来获取当前 ΔI。
     * 但为了解耦，这里独立计算：直接读取 ADC 原始值并和上一次对比。
     */
    static float32_t pt_i_prev[6] = {0};  // ← 每个电流通道独立缓存上次值

    /* 读取当前三相电流绝对值 */
    float32_t ia = (float32_t)adc_current_buf[ch_i_a] - CURRENT_ZERO_OFFSET;
    float32_t ib = (float32_t)adc_current_buf[ch_i_b] - CURRENT_ZERO_OFFSET;
    float32_t ic = (float32_t)adc_current_buf[ch_i_c] - CURRENT_ZERO_OFFSET;

    /* 计算三相电流变化率 ΔI = |I(n) - I(n-1)| */
    float32_t di_a = ia - pt_i_prev[ch_i_a]; if (di_a < 0) di_a = -di_a;
    float32_t di_b = ib - pt_i_prev[ch_i_b]; if (di_b < 0) di_b = -di_b;
    float32_t di_c = ic - pt_i_prev[ch_i_c]; if (di_c < 0) di_c = -di_c;

    /* 保存当前值供下次使用 */
    pt_i_prev[ch_i_a] = ia;
    pt_i_prev[ch_i_b] = ib;
    pt_i_prev[ch_i_c] = ic;

    /* 取三相绝对值用于过流判断 */
    if (ia < 0) ia = -ia;
    if (ib < 0) ib = -ib;
    if (ic < 0) ic = -ic;
    float32_t i_sum = ia + ib + ic;       // ← 三相电流绝对值之和

    /*
     * PT 断线判定条件 (三个条件必须同时满足)：
     *   ① 三相 ΔI 全部极小 (电流波形平稳如镜面)
     *   ② 三相电流绝对值之和未过流 (负荷正常运行中)
     *   → 说明实际线路没有任何异常，只是 PT 的测量信号丢了！
     */
    if (di_a < PT_BREAK_DI_CEILING &&     // ← A 相电流无跳变
        di_b < PT_BREAK_DI_CEILING &&     // ← B 相电流无跳变
        di_c < PT_BREAK_DI_CEILING &&     // ← C 相电流无跳变
        i_sum < (float32_t)(CURRENT_OVERCURRENT * 3)) {  // ← 且未过流
        return 1;                          // ← PT 断线确认！
    }
    return 0;
}

/*
 * ✅ [Embedded-Engineer]
 * 综合故障方向判断 — PT断线前置筛查 + 进线电压 dU/dt + DSP 极性功率验证
 *
 * 防误动判定矩阵 (升级版)：
 * ┌────────────────┬──────────────┬──────────────────────────┐
 * │ ΔU (电压突变)   │ ΔI (电流变化) │ 判定结果                  │
 * ├────────────────┼──────────────┼──────────────────────────┤
 * │ 有 (骤降)       │ 有 (剧烈)     │ 真故障 → 方向保护动作     │
 * │ 有 (骤降)       │ 无 (平稳)     │ PT断线 → 闭锁报警禁切换   │
 * │ 无              │ 有            │ 电流扰动 → 忽略           │
 * │ 无              │ 无            │ 正常运行                  │
 * └────────────────┴──────────────┴──────────────────────────┘
 */
static FaultType_t Judge_Fault_Direction(const AdcBatch_t *curr, const AdcBatch_t *prev)
{
    /* ── 第一步：计算四个位置的电压突变量 ── */
    int32_t dv_feeder1 = Calc_Position_Transient(curr, prev, V_FEEDER1_UAB, V_FEEDER1_UBC);
    int32_t dv_feeder2 = Calc_Position_Transient(curr, prev, V_FEEDER2_UAB, V_FEEDER2_UBC);
    int32_t dv_bus1    = Calc_Position_Transient(curr, prev, V_BUS_PT1_UAB, V_BUS_PT1_UBC);
    int32_t dv_bus2    = Calc_Position_Transient(curr, prev, V_BUS_PT2_UAB, V_BUS_PT2_UBC);

    uint8_t feeder1_dip = (dv_feeder1 > VOLTAGE_DIP_THRESHOLD); // ← 进线柜1 电压骤降？
    uint8_t feeder2_dip = (dv_feeder2 > VOLTAGE_DIP_THRESHOLD); // ← 进线柜2 电压骤降？
    uint8_t bus1_dip    = (dv_bus1    > VOLTAGE_DIP_THRESHOLD); // ← 母线1 电压骤降？
    uint8_t bus2_dip    = (dv_bus2    > VOLTAGE_DIP_THRESHOLD); // ← 母线2 电压骤降？

    /* ── 第 1.5 步 (前置筛查)：PT 断线防误动 ── */
    /*
     * 核心逻辑：只有某一路进线电压骤降，但对应的电流纹丝不动 → PT 传感器断了！
     * 如果是真故障，电流一定会跟着暴走 (过流或反向)。
     * 如果是 PT 断线，电流会保持原来平稳的负荷状态。
     */
    if (feeder1_dip && !feeder2_dip && !bus1_dip && !bus2_dip) {
        /* 仅进线柜1的电压掉了，其他位置全部正常 → 高度怀疑 PT 断线 */
        if (Check_PT_Broken(IDX_FEEDER1_IA, IDX_FEEDER1_IB, IDX_FEEDER1_IC)) {
            return FAULT_PT_BROKEN;   // ← PT1 断线！闭锁！
        }
    }
    if (feeder2_dip && !feeder1_dip && !bus1_dip && !bus2_dip) {
        /* 仅进线柜2的电压掉了 → 怀疑 PT 断线 */
        if (Check_PT_Broken(IDX_FEEDER2_IA, IDX_FEEDER2_IB, IDX_FEEDER2_IC)) {
            return FAULT_PT_BROKEN;   // ← PT2 断线！闭锁！
        }
    }
    if (bus1_dip && !bus2_dip && !feeder1_dip && !feeder2_dip) {
        /* 仅母线PT1掉了 → 怀疑母线 PT 断线 */
        if (Check_PT_Broken(IDX_FEEDER1_IA, IDX_FEEDER1_IB, IDX_FEEDER1_IC)) {
            return FAULT_PT_BROKEN;   // ← 母线 PT1 断线！
        }
    }

    /* ── 第二步：如果不是 PT 断线，进入真正的故障方向判断 ── */
    if (feeder1_dip) {
        FaultType_t dir = Calc_Directional_Power(curr, V_FEEDER1_UAB, IDX_FEEDER1_IA);
        if (dir == FAULT_FEEDER1) return FAULT_FEEDER1; // ← 外部故障，允许快切
        if (dir == FAULT_BUS)     return FAULT_BUS;     // ← 内部短路，绝对闭锁
    }

    if (feeder2_dip) {
        FaultType_t dir = Calc_Directional_Power(curr, V_FEEDER2_UAB, IDX_FEEDER2_IA);
        if (dir == FAULT_FEEDER1) return FAULT_FEEDER2; // ← 复用代号代表外网故障 (柜2方向)
        if (dir == FAULT_BUS)     return FAULT_BUS;     // ← 内部短路
    }

    /* 两段母线都骤降 = 母线故障预判 */
    if (bus1_dip && bus2_dip) {
        return FAULT_BUS;
    }

    /* 两路进线都骤降但母线没降 → 外网系统失电 → 默认保柜2 */
    if (feeder1_dip && feeder2_dip && !bus1_dip) {
        return FAULT_FEEDER1;
    }

    /* 无法判断，保守不动作 */
    return FAULT_NONE;
}

/* ═══════════════════════════════════════════════════════════════════════
 * 引擎③：K-Switch 防桥连换流引擎 (Break-Before-Make + ADC 闭环过零)
 *
 * 重构说明：
 *   原先的 Safe_Transfer_K1_to_K3 / Safe_Transfer_K3_to_K1 被废弃。
 *   替代为通用的 Safe_Closed_Loop_Transfer(strategy)，
 *   根据拓扑+故障类型查策略表得到 break/make 动作。
 * ═══════════════════════════════════════════════════════════════════════ */

#define KSWITCH_TIMEOUT_CYCLES (SystemCoreClock / 100) /* ← 10ms 超时 (50Hz 半周期 @180MHz) */
#define ZERO_CROSS_MARGIN      10    /* ← 电流过零死区阈值 (ADC LSB) */
#define ZERO_CROSS_CONFIRM_N   3     /* ← 连续 N 次确认 (约 117μs @25.6kHz) */

/* 拓扑自检参数 */
#define TOPO_INIT_SAMPLES      500   /* ← 500ms (500 个 1ms 周期) 自检窗口 */
#define VOLTAGE_HAS_THRESHOLD  1000  /* ← PT “有压”判定阈值 (约额定 10%) */

/*
 * ✅ [Embedded-Engineer]
 * 通用闭环换流函数：先断 (break) → ADC 过零确认 → 后合 (make)
 *
 * 此函数替代了原先的 Safe_Transfer_K1_to_K3 / Safe_Transfer_K3_to_K1。
 * 现在的先断/后合目标由策略路由表决定，不再硬编码。
 *
 * 参数：
 *   strategy — 从 KSwitch_LookupStrategy() 查表得到
 *   feeder_idx — 故障进线的电流通道索引 (0=FEEDER1, 3=FEEDER2)
 *
 * 返回：1=换流成功, 0=过零超时失败
 */
static uint8_t Safe_Closed_Loop_Transfer(const SwitchStrategy_t *strategy,
                                          uint8_t feeder_idx)
{
    /* ① 先断：执行 break_action */
    KSwitch_ExecuteBreak(strategy->break_action);                  /* ← 跳开故障侧开关 */

    /* ② 如果不需要后合 (并列模式仅跳K1)，直接返回成功 */
    if (strategy->make_action == ACT_NONE) {
        return 1;                                                   /* ← 仅断开即可 */
    }

    /* ③ ADC 闭环过零检测：等待断开侧电流归零 */
    uint32_t start_tick = DWT->CYCCNT;                              /* ← DWT 超时起点 */
    uint8_t zero_count = 0;

    while ((DWT->CYCCNT - start_tick) < KSWITCH_TIMEOUT_CYCLES) {  /* ← 10ms 硬超时 */
        int32_t ia = (int32_t)adc_current_buf[feeder_idx + 0] - CURRENT_ZERO_OFFSET;
        int32_t ib = (int32_t)adc_current_buf[feeder_idx + 1] - CURRENT_ZERO_OFFSET;
        int32_t ic = (int32_t)adc_current_buf[feeder_idx + 2] - CURRENT_ZERO_OFFSET;
        if (ia < 0) ia = -ia;
        if (ib < 0) ib = -ib;
        if (ic < 0) ic = -ic;

        if (ia < ZERO_CROSS_MARGIN && ib < ZERO_CROSS_MARGIN && ic < ZERO_CROSS_MARGIN) {
            zero_count++;
            if (zero_count >= ZERO_CROSS_CONFIRM_N) {
                /* ← 电流已确认归零！ */
                break;
            }
        } else {
            zero_count = 0;                                        /* ← 有波动，重新计数 */
        }
    }

    if (zero_count >= ZERO_CROSS_CONFIRM_N) {
        /* ④ 后合：执行 make_action */
        KSwitch_ExecuteMake(strategy->make_action);                /* ← 合上目标开关 */
        return 1;                                                   /* ← 换流成功 */
    } else {
        /* ⑤ 超时！终极防线：全闭锁 */
        KSwitch_ExecuteBreak(ACT_LOCKOUT_ALL);                     /* ← K1+K2+K3 全跳 */
        g_fts_state = FTS_STATE_LOCKOUT;
        return 0;                                                   /* ← 换流失败 */
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * 引擎④：FTS 保护主任务 (稳态状态机管家 + SPLL 路由)
 *
 * 状态机转移图：
 *
 *   ┌──────────┐  dU/dt越限   ┌──────────────┐  确认+方向   ┌───────────┐
 *   │ NORMAL   │─────────────→│ VOLTAGE_DIP  │─────────────→│ TRIPPED   │
 *   └──────────┘              └──────────────┘   Δθ<20°     └───────────┘
 *        ↑ 恢复正常                   │
 *        │                            │ Δθ≥20° (相角撕裂)
 *        │                            ↓
 *        │                     ┌──────────────┐  Δθ→0°      ┌───────────┐
 *        └─────────────────────│ SYNC_WAIT    │─────────────→│ TRIPPED   │
 *                              └──────────────┘ 同期捕捉     └───────────┘
 *                                     │
 *                                     │ 超时2s
 *                                     ↓
 *                              ┌──────────────┐
 *                              │   LOCKOUT    │← 母线故障/换流超时
 *                              └──────────────┘
 * ═══════════════════════════════════════════════════════════════════════ */

static void FTS_ProtectTask(void *pvParameters)
{
    (void)pvParameters;

    FtsState_t       state = FTS_STATE_INIT;             /* ← 上电从INIT开始，而非NORMAL */
    FaultType_t      fault = FAULT_NONE;
    uint8_t          confirm_cnt = 0;
    uint8_t          prev_batch_idx = 0;
    uint32_t         sync_wait_start = 0;
    uint16_t         init_counter = 0;                    /* ← INIT 阶段 1ms 计数器 */
    uint16_t         tripped_counter = 0;                 /* ← TRIPPED 暂态等待计数器 */
    SwitchStrategy_t active_strategy = {ACT_NONE, ACT_NONE, 0}; /* ← 当前激活的切换策略 */

    vTaskDelay(pdMS_TO_TICKS(100));                       /* ← 等待 ADC/DMA 完成首次填充 */

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // ← 等待 ISR 的批量数据就绪通知

        /* DWT 计时起点 */
        DWT_StartMeasure(&fts_measure_algo);

        uint8_t ridx = ad7606_pp_buf.read_idx;
        const AdcBatch_t *curr = &ad7606_pp_buf.buffer[ridx];
        const AdcBatch_t *prev = &ad7606_pp_buf.buffer[prev_batch_idx];

        /* ── 每次唤醒都更新两路 SPLL (不论状态机处于哪个状态) ── */
        float32_t uab1 = (float32_t)curr->ch[V_FEEDER1_UAB][POINTS_PER_BATCH - 1]; // ← 进线柜1 Uab 最新采样
        float32_t ubc1 = (float32_t)curr->ch[V_FEEDER1_UBC][POINTS_PER_BATCH - 1]; // ← 进线柜1 Ubc 最新采样
        SPLL_Update_Line2Line(&spll_main, uab1, ubc1);                             // ← 更新主电源锁相环

        float32_t uab2 = (float32_t)curr->ch[V_FEEDER2_UAB][POINTS_PER_BATCH - 1]; // ← 进线柜2 Uab 最新采样
        float32_t ubc2 = (float32_t)curr->ch[V_FEEDER2_UBC][POINTS_PER_BATCH - 1]; // ← 进线柜2 Ubc 最新采样
        SPLL_Update_Line2Line(&spll_backup, uab2, ubc2);                           // ← 更新备用电源锁相环

        /* ─── 状态机 ─── */
        switch (state) {

        /* ──────────── 上电自检 (500ms 拓扑推断) ──────────── */
        case FTS_STATE_INIT: {
            /*
             * ✅ [Embedded-Engineer] 拓扑自检引擎：
             *
             *   上电后强制 500ms 自检期，执行以下操作：
             *   ① 每 1ms 读取 K-Switch DI 位置 (带消抖)
             *   ② 持续更新 SPLL 锁相
             *   ③ 500ms 到达后，取 PT 电压均值判断"有压/无压"
             *   ④ DI位置 + PT电压 交叉校验 → 确诊拓扑
             *   ⑤ 自洽 → NORMAL，矛盾 → LOCKOUT
             */
            KSwitch_ReadPosition();                              /* ← 每 1ms 读一次 DI (消抖) */
            init_counter++;

            if (init_counter >= TOPO_INIT_SAMPLES) {
                /* 500ms 到达，判定 PT 有压状态 */
                KSwitchPos_t pos = g_kswitch_pos;

                /* 用最后一拍的 PT 电压幅值判定"有压" */
                int32_t bus1_v = Calc_Position_Transient(curr, prev, V_BUS_PT1_UAB, V_BUS_PT1_UBC);
                int32_t bus2_v = Calc_Position_Transient(curr, prev, V_BUS_PT2_UAB, V_BUS_PT2_UBC);
                /*
                 * 注意：这里用 Calc_Position_Transient 返回的是变化率，
                 *       稳态下变化量应该很小。直接读 ADC 值判断有压更精确：
                 */
                uint8_t bus1_has_v = 0, bus2_has_v = 0;
                for (int p = 0; p < POINTS_PER_BATCH; p++) {
                    int16_t v1 = curr->ch[V_BUS_PT1_UAB][p];
                    int16_t v2 = curr->ch[V_BUS_PT2_UAB][p];
                    if (v1 > VOLTAGE_HAS_THRESHOLD || v1 < -VOLTAGE_HAS_THRESHOLD) bus1_has_v = 1;
                    if (v2 > VOLTAGE_HAS_THRESHOLD || v2 < -VOLTAGE_HAS_THRESHOLD) bus2_has_v = 1;
                }

                /* 交叉校验推断拓扑 */
                g_topology = KSwitch_DetectTopology(&pos, bus1_has_v, bus2_has_v);

                if (g_topology == TOPO_ERROR || g_topology == TOPO_UNKNOWN) {
                    /* DI与PT矛盾 → 绝对闭锁！ */
                    state = FTS_STATE_LOCKOUT;
                } else if (g_topology == TOPO_MAINTENANCE) {
                    /* 检修方式 → 闭锁 (全分不允许自动动作) */
                    state = FTS_STATE_LOCKOUT;
                } else {
                    /* 拓扑自检通过 → 进入稳态监视 */
                    state = FTS_STATE_NORMAL;
                }
            }
            break;
        }

        /* ──────────── 稳态监视 ──────────── */
        case FTS_STATE_NORMAL: {
            /* 持续更新 K-Switch DI 位置 (检测运行中的拓扑变化) */
            KSwitch_ReadPosition();

            /*
             * ✅ [Embedded-Engineer] 备用电源可用性预判：
             *
             * 在分段运行模式下，离线侧电源的 PT 应该仍有压(只是没并网)。
             * 如果离线侧进线 PT 无压，说明备用电源已故障，切换后会合到坦电源上。
             * 此时应标记 backup_available=0，策略路由时直接闭锁而非盲切。
             */
            {
                int32_t feeder1_v = Get_Position_Voltage(curr, V_FEEDER1_UAB, V_FEEDER1_UBC);
                int32_t feeder2_v = Get_Position_Voltage(curr, V_FEEDER2_UAB, V_FEEDER2_UBC);

                if (g_topology == TOPO_SPLIT_SRC1) {
                    /* 电源1带全所，K3分，检测电源2是否可用 */
                    g_backup_available = (feeder2_v > VOLTAGE_HAS_THRESHOLD) ? 1 : 0;
                } else if (g_topology == TOPO_SPLIT_SRC2) {
                    /* 电源2带全所，K1分，检测电源1是否可用 */
                    g_backup_available = (feeder1_v > VOLTAGE_HAS_THRESHOLD) ? 1 : 0;
                } else if (g_topology == TOPO_PARALLEL) {
                    /* 并列运行，两路都在线，互为备用 */
                    g_backup_available = (feeder1_v > VOLTAGE_HAS_THRESHOLD &&
                                          feeder2_v > VOLTAGE_HAS_THRESHOLD) ? 1 : 0;
                } else {
                    g_backup_available = 0;
                }
            }

            int32_t dv_max = 0;
            for (int pos = 0; pos < 8; pos += 2) {
                int32_t dv = Calc_Position_Transient(curr, prev, pos, pos + 1);
                if (dv > dv_max) dv_max = dv;
            }
            if (dv_max > VOLTAGE_DIP_THRESHOLD) {
                state = FTS_STATE_VOLTAGE_DIP;
                confirm_cnt = 1;
                fault = Judge_Fault_Direction(curr, prev);

                /* 故障触发瞬间，刷新拓扑快照用于策略查询 */
                g_topology = KSwitch_DetectTopology(
                    (const KSwitchPos_t *)&g_kswitch_pos,
                    1, 1);  /* 简化：故障前两段母线都应该有压 */
            }
            break;
        }

        /* ──────────── 电压骤降确认 + SPLL 路由决策 ──────────── */
        case FTS_STATE_VOLTAGE_DIP: {
            int32_t dv_max = 0;
            for (int pos = 0; pos < 8; pos += 2) {
                int32_t dv = Calc_Position_Transient(curr, prev, pos, pos + 1);
                if (dv > dv_max) dv_max = dv;
            }
            if (dv_max > VOLTAGE_DIP_THRESHOLD) {
                confirm_cnt++;
                fault = Judge_Fault_Direction(curr, prev);

                if (confirm_cnt >= TRIP_CONFIRM_COUNT && fault != FAULT_NONE) {

                    /* ──── PT 断线：绝对不允许动作！立刻闭锁报警 ──── */
                    if (fault == FAULT_PT_BROKEN) {
                        /*
                         * ✅ [Embedded-Engineer] PT 断线处理：
                         *   电压测量回路已失效，系统已变成"瞎子"！
                         *   绝对禁止任何切换动作，否则可能切断正常运行的负载！
                         *   只能报警并等待运维人员到场检查 PT 保险丝和接线。
                         */
                        state = FTS_STATE_LOCKOUT;                 // ← 直接进入闭锁
                        FaultRecorder_Trigger();                   // ← 锁定触发帧，开始后采集
                        /* 不触发任何 K-Switch！保持当前供电状态不变！ */
                        break;
                    }

                    /* 获取当前两路电源的相角差 (弧度) */
                    float32_t delta_theta = SPLL_Get_Phase_Difference();
                    float32_t abs_dt = delta_theta;
                    if (abs_dt < 0.0f) abs_dt = -abs_dt;          // ← fabsf 的替代 (不引入 math.h)

                    /*
                     * ✅ [Embedded-Engineer] 策略路由矩阵：
                     *   根据 g_topology + fault 查表得到 break/make 动作
                     *   彻底取代了硬编码的 Safe_Transfer_K1_to_K3 / K3_to_K1
                     */
                    active_strategy = KSwitch_LookupStrategy(g_topology, (uint8_t)fault);

                    /* 母线故障或策略为全闭锁 → 直接跳闸 */
                    if (fault == FAULT_BUS || active_strategy.break_action == ACT_LOCKOUT_ALL) {
                        KSwitch_ExecuteBreak(ACT_LOCKOUT_ALL);    /* ← K1+K2+K3+K4 全跳 */
                        state = FTS_STATE_LOCKOUT;
                        FaultRecorder_Trigger();
                        break;
                    }

                    /*
                     * ✅ [Embedded-Engineer] 备用电源可用性安全栏：
                     *
                     * 如果策略需要合闸 (make_action != ACT_NONE)，但备用电源不可用，
                     * 说明切换后会合到一个已故障的电源上 → 全所失电！
                     * 此时直接闭锁，等运维人员排查。
                     */
                    if (active_strategy.make_action != ACT_NONE && !g_backup_available) {
                        KSwitch_ExecuteBreak(active_strategy.break_action); /* ← 先断故障侧 */
                        state = FTS_STATE_LOCKOUT;                          /* ← 但不合闸！ */
                        FaultRecorder_Trigger();
                        break;
                    }

                    /* SPLL 相角差路由 */
                    if (active_strategy.need_sync && abs_dt >= SYNC_ANGLE_FAST) {
                        /*
                         * Δθ ≥ 20° 且策略要求同期 → 先断坏电源，等SPLL追上
                         */
                        KSwitch_ExecuteBreak(active_strategy.break_action);
                        sync_wait_start = xTaskGetTickCount();
                        state = FTS_STATE_SYNC_WAIT;
                    } else {
                        /*
                         * Δθ < 20° 或不需要同期 (并列模式) → 极速闭环换流！
                         */
                        uint8_t feeder = (fault == FAULT_FEEDER1) ? IDX_FEEDER1_IA : IDX_FEEDER2_IA;
                        if (Safe_Closed_Loop_Transfer(&active_strategy, feeder)) {
                            state = FTS_STATE_TRIPPED;
                        } else {
                            state = FTS_STATE_LOCKOUT;             /* ← 过零超时 */
                        }
                        FaultRecorder_Trigger();
                    }
                }
            } else {
                /* 电压恢复正常 → 回到稳态监视 */
                state = FTS_STATE_NORMAL;
                confirm_cnt = 0;
                fault = FAULT_NONE;
            }
            break;
        }

        /* ──────────── 同期捕捉等待 (SPLL 相角追踪) ──────────── */
        case FTS_STATE_SYNC_WAIT: {
            /*
             * ✅ [Embedded-Engineer] 无扰动同期捕捉核心逻辑：
             *
             * 物理原理：当主电源跳开后，母线上的电动机负载靠惯性继续旋转，
             * 产生衰减的残压。残压频率会逐渐降低 (转子减速)，导致 Δθ 不断变化。
             * SPLL 持续跟踪两路电源的相位，当 Δθ 自然趋近 0° 时，
             * 就是合闸的黄金窗口！
             *
             * 合闸条件：|Δθ| < 5°
             * 超时条件：2秒内 Δθ 未收敛 → 残压已衰减过多，闭锁
             */
            float32_t delta_theta = SPLL_Get_Phase_Difference();
            float32_t abs_dt = delta_theta;
            if (abs_dt < 0.0f) abs_dt = -abs_dt;

            if (abs_dt < SYNC_ANGLE_OK) {
                /* Δθ < 5° → 黄金窗口！立刻执行策略中的后合动作 */
                KSwitch_ExecuteMake(active_strategy.make_action);  /* ← 策略路由合闸 */
                state = FTS_STATE_TRIPPED;
                FaultRecorder_Trigger();
            }
            else if ((xTaskGetTickCount() - sync_wait_start) > pdMS_TO_TICKS(SYNC_TIMEOUT_MS)) {
                /* 2秒超时：残压衰减到无法同期，全闭锁 */
                KSwitch_ExecuteBreak(ACT_LOCKOUT_ALL);             /* ← K1+K2+K3 全跳 */
                state = FTS_STATE_LOCKOUT;
                FaultRecorder_Trigger();
            }
            /* 否则继续等待下一个 1ms 更新 */
            break;
        }

        /* ──────────── 已切换完成 (500ms 暂态等待 + 拓扑刷新) ──────────── */
        case FTS_STATE_TRIPPED: {
            /*
             * ✅ [Embedded-Engineer] 安全审计修复：非阻塞暂态等待
             *
             * K-Switch 断路器/晶闸管从"脉冲发出"到"辅助触点物理翻转"
             * 需要 50~200ms 机械+电弧暂态。在此期间：
             *   - DI 辅助触点可能处于中间位 → 读出 0xFF → 误判 TOPO_ERROR
             *   - PT 电压存在电弧重燃暂态 → 有压/无压判定不准
             *
             * 因此必须等待 500ms 暂态结束后，才能信任 DI/PT 的读数。
             *
             * ⚠️ 绝对禁止 vTaskDelay / HAL_Delay 挂起保护主任务！
             *    使用 tripped_counter 每 1ms 递增，纯非阻塞。
             */
            KSwitch_ReadPosition();                                    /* ← 每 1ms 持续消抖积累 */
            tripped_counter++;

            if (tripped_counter >= TOPO_INIT_SAMPLES) {                /* 500ms 暂态结束 */
                /* 用暂态后的稳态 PT 电压判定新拓扑 */
                uint8_t bus1_v = 0, bus2_v = 0;
                for (int p = 0; p < POINTS_PER_BATCH; p++) {
                    int16_t v1 = curr->ch[V_BUS_PT1_UAB][p];
                    int16_t v2 = curr->ch[V_BUS_PT2_UAB][p];
                    if (v1 > VOLTAGE_HAS_THRESHOLD || v1 < -VOLTAGE_HAS_THRESHOLD) bus1_v = 1;
                    if (v2 > VOLTAGE_HAS_THRESHOLD || v2 < -VOLTAGE_HAS_THRESHOLD) bus2_v = 1;
                }
                g_topology = KSwitch_DetectTopology(
                    (const KSwitchPos_t *)&g_kswitch_pos, bus1_v, bus2_v);

                if (g_topology == TOPO_ERROR || g_topology == TOPO_UNKNOWN) {
                    /* DI/PT 矛盾 → 闭锁 */
                    state = FTS_STATE_LOCKOUT;
                } else {
                    /* 拓扑自洽 → 回到正常监视，准备响应可能的第二次故障 */
                    state = FTS_STATE_NORMAL;
                    fault = FAULT_NONE;
                    confirm_cnt = 0;
                }
                tripped_counter = 0;                                   /* ← 复位计数器 */
            }
            /* 否则继续等待下一个 1ms 周期 */
            break;
        }

        /* ──────────── 绝对闭锁 ──────────── */
        case FTS_STATE_LOCKOUT: {
            /*
             * ✅ [Embedded-Engineer] 闭锁状态处理：
             *
             * 全站解列后，必须持续确保所有开关保持分开。
             * 只有人工复位 (B板发送复位命令) 才能解除闭锁。
             *
             * 全局状态已通过 g_fts_state/g_fault_type 导出，
             * B 板 SPI4 轮询时会读到 FTS_STATE_LOCKOUT + 具体故障类型，
             * 屏幕上显示严重告警。
             */
            break;
        }
        }

        /* 更新全局状态 (供 SPI4 快照读取) */
        g_fts_state = state;
        g_fault_type = fault;
        prev_batch_idx = ridx;                                     // ← 保存本批次索引供下次对比

        /*
         * ✅ [Embedded-Engineer] 垫底推流：SDRAM 录波帧写入
         *
         * ⚠️ 时序约束 (为什么放在这里)：
         *   memcpy 400B 到 SDRAM 需要经过 FMC 总线，耗时约 5~10μs。
         *   必须放在所有 DSP 计算和 K-Switch 动作之后，
         *   确保 FMC 总线操作永远不会阻塞核心保护逻辑。
         *
         *   执行顺序保证：
         *     ① SPLL 更新 (纯 CPU 计算)      ← 最高优先级
         *     ② 状态机判断 + K-Switch 动作     ← 第二优先级
         *     ③ 全局状态变量更新               ← 第三优先级
         *     ④ FaultRecorder_PushFrame()      ← 垫底 (FMC 总线操作)
         *     ⑤ DWT_StopMeasure()             ← 计时终点 (包含FMC开销)
         */
        FaultRecorder_PushFrame(curr);                             // ← 将 400B 波形帧推入 SDRAM Ring Buffer

        /* DWT 计时终点 (包含 SDRAM 写入开销，可精确评估 FMC 影响) */
        DWT_StopMeasure(&fts_measure_algo);
    }
}

/* ═══════════ 对外接口 ═══════════ */

void FTS_Protect_Init(void)
{
    /* ── K-Switch 驱动 + 反馈 GPIO 初始化 (引脚定义在 bsp_kswitch_di.h) ── */
    KSwitch_Drive_Init();                                           /* ← TRIGGER DO: PI4~PI9/PE3/PC13 */
    KSwitch_DI_Init();                                              /* ← POSITION DI: PI1~PI3/PH13~PH15/PA10/PA12 */

    /* ── SPLL 初始化 (两路电源各一个锁相环) ── */
    SPLL_Init(&spll_main);                                          // ← 主电源 PLL
    SPLL_Init(&spll_backup);                                        // ← 备用电源 PLL

    /* ── SDRAM 故障录波初始化 ── */
    FaultRecorder_Init();                                           // ← Head/Tail 归零，SDRAM 区域就位

    /* ── 创建保护主任务 (最高优先级) ── */
    xTaskCreate(FTS_ProtectTask, "FTS_Prot", 512, NULL,
                configMAX_PRIORITIES - 1, &xFtsProtectTaskHandle);
}
