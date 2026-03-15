/**
 * @file    ui_chart_binding.h
 * @brief   Tab2 历史曲线 — 5 个 chart 控件的数据绑定层
 *
 *  ┌───────────── 自查验证 (embedded-engineer 规则) ─────────────┐
 *  │ 控件         │ 所在子tab  │ 旁边Label    │ 绑定数据         │
 *  │ chartKV6     │ 温度曲线   │ "温度(℃)"    │ internal_adc[]   │
 *  │ chartKV2     │ 电流曲线   │ "电流(A)"    │ ch[3~5]          │
 *  │ chartKV3     │ 电流谐波   │ "电流(A)"    │ ch[3~5] (TODO FFT)│
 *  │ chartKV7     │ 电压曲线   │ "电压(KV)"   │ ch[0~2]          │
 *  │ chartKV8     │ 电压谐波   │ "电压(KV)"   │ ch[0~2] (TODO FFT)│
 *  └──────────────┴───────────┴─────────────┴─────────────────┘
 *
 *  Dropdown2: "上触头/下触头/电缆头/母线排" → 切换温度数据源
 */
#ifndef UI_CHART_BINDING_H
#define UI_CHART_BINDING_H

#include "eric888_spi_protocol.h"

/**
 * @brief  初始化 5 个 chart 的 series 句柄 + Dropdown2 事件回调
 * @note   必须在 ui_init() 之后调用
 * @retval 0=成功, -1=chart 控件未找到
 */
int ui_chart_binding_init(void);

/**
 * @brief  推送一组 ADC 数据到 chart (追加数据点 + 自动滚动)
 * @param  adc: 从 s_latest_adc 拷贝的本地副本
 * @note   建议 100ms 调用一次 (10Hz)
 */
void ui_chart_binding_update(const Eric888_ADC_Data *adc);

/**
 * @brief  暂停/恢复 chart 刷新 (切换到非波形 tab 时暂停)
 * @param  pause: 1=暂停, 0=恢复
 */
void ui_chart_binding_set_pause(uint8_t pause);

#endif /* UI_CHART_BINDING_H */
