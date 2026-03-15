/**
 * @file    ui_chart_binding.c
 * @brief   Tab2 历史曲线 — 5 个 chart 的数据绑定实现
 *
 *  数据流向:
 *  CommTask → s_latest_adc (mutex) → DisplayTask
 *      → ui_chart_binding_update() → lv_chart_set_next_value()
 *
 *  ┌─────────── 自查验证结果 (从 ui_scrmain.c Label 文字确认) ───────────┐
 *  │                                                                       │
 *  │  子tab "温度曲线" → chartKV6 (Label: "温度(℃)", A/B/C曲线)          │
 *  │    Y: -20~100, 绑定: internal_adc[0~2] 或 [3~5] (Dropdown2 切换)    │
 *  │    Dropdown2: "上触头/下触头/电缆头/母线排"                          │
 *  │                                                                       │
 *  │  子tab "电流曲线" → chartKV2 (Label: "电流(A)", A/B/C相)            │
 *  │    Y: 0~700, 绑定: ch[3]=Ia, ch[4]=Ib, ch[5]=Ic                    │
 *  │                                                                       │
 *  │  子tab "电流谐波" → chartKV3 (Label: "电流(A)", A/B/C相)            │
 *  │    Y: -20~100, 绑定: ch[3~5] (TODO: 后续实现 FFT 谐波分析)          │
 *  │                                                                       │
 *  │  子tab "电压曲线" → chartKV7 (Label: "电压(KV)", A/B/C相)           │
 *  │    Y: 0~35, 绑定: ch[0]=Ua, ch[1]=Ub, ch[2]=Uc                     │
 *  │                                                                       │
 *  │  子tab "电压谐波" → chartKV8 (Label: "电压(KV)", A/B/C相)           │
 *  │    Y: 0~35, 绑定: ch[0~2] (TODO: 后续实现 FFT 谐波分析)             │
 *  │                                                                       │
 *  └───────────────────────────────────────────────────────────────────────┘
 *
 *  缩放系数 (从 freertos.c line 243~246 确认):
 *    电压: ch[n] × 0.01 = kV   → chart Y 值 = ch[n] / 100
 *    电流: ch[n] × 0.001 = A   → chart Y 值 = ch[n] / 10
 *    温度: internal_adc[n] × 0.1 = ℃ → chart Y 值 = internal_adc[n] / 10
 */
#include "ui_chart_binding.h"
#include "lvgl.h"
#include "ui/ui.h"

/* ========================= 内部状态 ========================= */

/* 每个 chart 绑定结构 */
typedef struct {
    lv_obj_t          *chart;   /* ← chart 控件句柄 (来自 SquareLine) */
    lv_chart_series_t *ser[3];  /* ← 3 条曲线: [0]=A相/黄 [1]=B相/绿 [2]=C相/红 */
} ChartBinding_t;

static ChartBinding_t s_bindings[5];  /* ← 5 个 chart 绑定 */
static uint8_t s_paused = 0;         /* ← 暂停标志 (非波形页时暂停) */

/*
 * 温度测点选择 (由 Dropdown2 控制)
 * 0 = 上触头 → internal_adc[0,1,2]
 * 1 = 下触头 → internal_adc[3,4,5]
 * 2 = 电缆头 → 暂无数据源(显示0)
 * 3 = 母线排 → 暂无数据源(显示0)
 */
static uint8_t s_temp_source = 0;

/* ========================= 辅助函数 ========================= */

/**
 * @brief  获取 chart 的第 N 条 series
 * @note   SquareLine 按顺序 add_series: [0]=黄 [1]=绿 [2]=红
 *         LVGL 的 get_series_next(NULL) 返回第一条，再 next 返回下一条
 */
static lv_chart_series_t *_get_series(lv_obj_t *chart, uint8_t idx)
{
    lv_chart_series_t *ser = lv_chart_get_series_next(chart, NULL); /* ← 首条 */
    for (uint8_t i = 0; i < idx && ser != NULL; i++) {
        ser = lv_chart_get_series_next(chart, ser);  /* ← 遍历到目标 */
    }
    return ser;
}

/**
 * @brief  初始化单个 chart 绑定
 * @param  b:     绑定结构体
 * @param  chart: SquareLine 创建的 chart 控件
 * @note   不修改 Y 轴范围 — 保持 SquareLine 原始设计值
 *         (已通过自查确认 Y 范围与数据量级匹配)
 */
static int _init_chart(ChartBinding_t *b, lv_obj_t *chart)
{
    if (chart == NULL) return -1; /* ← 防御: 控件不存在 */

    b->chart = chart;
    b->ser[0] = _get_series(chart, 0); /* ← A相/黄色 */
    b->ser[1] = _get_series(chart, 1); /* ← B相/绿色 */
    b->ser[2] = _get_series(chart, 2); /* ← C相/红色 */

    /*
     * SquareLine 用 lv_chart_set_ext_y_array() 设了只有1个元素的外部数组。
     * 调用 set_point_count 会让 LVGL 重新分配内部数组，
     * 之后 lv_chart_set_next_value() 才能正常滚动。
     * 点数保持 SquareLine 原始设计的 384 点不变。
     */
    lv_chart_set_point_count(chart, 384); /* ← 384 点滚动窗口 */
    lv_chart_refresh(chart);              /* ← 清空历史，从空白开始 */

    return 0;
}

/**
 * @brief  Dropdown2 值变化回调 — 切换温度测点数据源
 *
 *  选项 0 = "上触头" → internal_adc[0,1,2]
 *  选项 1 = "下触头" → internal_adc[3,4,5]
 *  选项 2 = "电缆头" → 暂无 (显示 0)
 *  选项 3 = "母线排" → 暂无 (显示 0)
 */
static void _dropdown2_event_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);  /* ← 获取 Dropdown2 控件 */
    s_temp_source = (uint8_t)lv_dropdown_get_selected(dd); /* ← 0/1/2/3 */

    /* 切换测点时清空温度 chart 历史，避免新旧数据混在一起 */
    if (s_bindings[0].chart != NULL) {
        lv_chart_set_point_count(s_bindings[0].chart, 384);
        lv_chart_refresh(s_bindings[0].chart);
    }
}

/* ========================= 公共 API ========================= */

int ui_chart_binding_init(void)
{
    int ret = 0;

    /* ── chartKV6: 温度曲线 ──
     * 自查: 子tab="温度曲线", Label="温度(℃)", Y=-20~100
     * 数据: internal_adc[0~2](上触头) 或 [3~5](下触头)
     * 缩放: × 0.1 = ℃, chart Y = internal_adc[n] / 10
     */
    ret |= _init_chart(&s_bindings[0], ui_chartKV6);

    /* ── chartKV2: 电流曲线 ──
     * 自查: 子tab="电流曲线", Label="电流(A)", Y=0~700
     * 数据: ch[3]=Ia, ch[4]=Ib, ch[5]=Ic
     * 缩放: × 0.001 = A, chart Y = ch[n] / 10 (显示 0.01A 精度)
     */
    ret |= _init_chart(&s_bindings[1], ui_chartKV2);

    /* ── chartKV3: 电流谐波 ──
     * 自查: 子tab="电流谐波", Label="电流(A)", Y=-20~100
     * 数据: TODO — 需要 FFT 谐波分析，暂绑 ch[3~5] 原始值
     */
    ret |= _init_chart(&s_bindings[2], ui_chartKV3);

    /* ── chartKV7: 电压曲线 ──
     * 自查: 子tab="电压曲线", Label="电压(KV)", Y=0~35
     * 数据: ch[0]=Ua, ch[1]=Ub, ch[2]=Uc
     * 缩放: × 0.01 = kV, chart Y = ch[n] / 100
     */
    ret |= _init_chart(&s_bindings[3], ui_chartKV7);

    /* ── chartKV8: 电压谐波 ──
     * 自查: 子tab="电压谐波", Label="电压(KV)", Y=0~35
     * 数据: TODO — 需要 FFT 谐波分析，暂绑 ch[0~2] 原始值
     */
    ret |= _init_chart(&s_bindings[4], ui_chartKV8);

    /* 注册 Dropdown2 事件回调 — 切换温度测点 */
    if (ui_Dropdown2 != NULL) {
        lv_obj_add_event_cb(ui_Dropdown2, _dropdown2_event_cb,
                            LV_EVENT_VALUE_CHANGED, NULL);
    }

    return ret;
}

void ui_chart_binding_update(const Eric888_ADC_Data *adc)
{
    if (adc == NULL || s_paused) return; /* ← 空指针或暂停时跳过 */

    /* ──── chartKV6: 温度曲线 (internal_adc) ────
     * Dropdown2 选择:
     *   0="上触头" → adc[0,1,2]
     *   1="下触头" → adc[3,4,5]
     *   2/3 = 暂无数据源 → 显示 0
     */
    if (s_bindings[0].chart != NULL) {
        int16_t ta, tb, tc; /* ← A/B/C 三相温度值 */
        if (s_temp_source == 0) {
            /* 上触头: internal_adc[0]=A, [1]=B, [2]=C */
            ta = (int16_t)(adc->internal_adc[0] / 10); /* ← ×0.1℃ → ℃ */
            tb = (int16_t)(adc->internal_adc[1] / 10);
            tc = (int16_t)(adc->internal_adc[2] / 10);
        } else if (s_temp_source == 1) {
            /* 下触头: internal_adc[3]=A, [4]=B, [5]=C */
            ta = (int16_t)(adc->internal_adc[3] / 10);
            tb = (int16_t)(adc->internal_adc[4] / 10);
            tc = (int16_t)(adc->internal_adc[5] / 10);
        } else {
            /* 电缆头/母线排: 暂无数据源 */
            ta = tb = tc = 0;
        }
        if (s_bindings[0].ser[0])
            lv_chart_set_next_value(s_bindings[0].chart,
                                    s_bindings[0].ser[0], ta); /* ← A曲线/黄 */
        if (s_bindings[0].ser[1])
            lv_chart_set_next_value(s_bindings[0].chart,
                                    s_bindings[0].ser[1], tb); /* ← B曲线/绿 */
        if (s_bindings[0].ser[2])
            lv_chart_set_next_value(s_bindings[0].chart,
                                    s_bindings[0].ser[2], tc); /* ← C曲线/红 */
    }

    /* ──── chartKV2: 电流曲线 (ch[3~5]) ────
     * ch[3]=Ia, ch[4]=Ib, ch[5]=Ic
     * 原始值 ÷ 10 → 0.01A 精度 (Y: 0~700)
     */
    if (s_bindings[1].chart != NULL) {
        if (s_bindings[1].ser[0])
            lv_chart_set_next_value(s_bindings[1].chart,
                                    s_bindings[1].ser[0],
                                    (lv_coord_t)(adc->ch[3] / 10)); /* ← Ia */
        if (s_bindings[1].ser[1])
            lv_chart_set_next_value(s_bindings[1].chart,
                                    s_bindings[1].ser[1],
                                    (lv_coord_t)(adc->ch[4] / 10)); /* ← Ib */
        if (s_bindings[1].ser[2])
            lv_chart_set_next_value(s_bindings[1].chart,
                                    s_bindings[1].ser[2],
                                    (lv_coord_t)(adc->ch[5] / 10)); /* ← Ic */
    }

    /* ──── chartKV3: 电流谐波 (ch[3~5]) ────
     * TODO: 后续实现 FFT 谐波分析
     * 临时: 绑原始电流值显示波形
     */
    if (s_bindings[2].chart != NULL) {
        if (s_bindings[2].ser[0])
            lv_chart_set_next_value(s_bindings[2].chart,
                                    s_bindings[2].ser[0],
                                    (lv_coord_t)(adc->ch[3] / 10));
        if (s_bindings[2].ser[1])
            lv_chart_set_next_value(s_bindings[2].chart,
                                    s_bindings[2].ser[1],
                                    (lv_coord_t)(adc->ch[4] / 10));
        if (s_bindings[2].ser[2])
            lv_chart_set_next_value(s_bindings[2].chart,
                                    s_bindings[2].ser[2],
                                    (lv_coord_t)(adc->ch[5] / 10));
    }

    /* ──── chartKV7: 电压曲线 (ch[0~2]) ────
     * ch[0]=Ua, ch[1]=Ub, ch[2]=Uc
     * 原始值 ÷ 100 → kV (Y: 0~35)
     */
    if (s_bindings[3].chart != NULL) {
        if (s_bindings[3].ser[0])
            lv_chart_set_next_value(s_bindings[3].chart,
                                    s_bindings[3].ser[0],
                                    (lv_coord_t)(adc->ch[0] / 100)); /* ← Ua */
        if (s_bindings[3].ser[1])
            lv_chart_set_next_value(s_bindings[3].chart,
                                    s_bindings[3].ser[1],
                                    (lv_coord_t)(adc->ch[1] / 100)); /* ← Ub */
        if (s_bindings[3].ser[2])
            lv_chart_set_next_value(s_bindings[3].chart,
                                    s_bindings[3].ser[2],
                                    (lv_coord_t)(adc->ch[2] / 100)); /* ← Uc */
    }

    /* ──── chartKV8: 电压谐波 (ch[0~2]) ────
     * TODO: 后续实现 FFT 谐波分析
     * 临时: 绑原始电压值显示波形
     */
    if (s_bindings[4].chart != NULL) {
        if (s_bindings[4].ser[0])
            lv_chart_set_next_value(s_bindings[4].chart,
                                    s_bindings[4].ser[0],
                                    (lv_coord_t)(adc->ch[0] / 100));
        if (s_bindings[4].ser[1])
            lv_chart_set_next_value(s_bindings[4].chart,
                                    s_bindings[4].ser[1],
                                    (lv_coord_t)(adc->ch[1] / 100));
        if (s_bindings[4].ser[2])
            lv_chart_set_next_value(s_bindings[4].chart,
                                    s_bindings[4].ser[2],
                                    (lv_coord_t)(adc->ch[2] / 100));
    }
}

void ui_chart_binding_set_pause(uint8_t pause)
{
    s_paused = pause; /* ← 0=恢复, 1=暂停 */
}
