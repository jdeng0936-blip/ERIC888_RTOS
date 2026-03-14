/**
 * @file   ui_fts_status.h
 * @brief  FTS 保护状态 UI 面板 — LVGL 8.x
 *
 * ✅ [Embedded-Engineer]
 * 数据来源：A板 → SPI4 → FTS_StatusSnapshot_t
 * 显示内容：保护状态 / 故障类型 / 运行拓扑 / 相角差 / 备用电源 / 录波冻结
 * LOCKOUT 时弹出全屏红色告警
 */
#ifndef UI_FTS_STATUS_H
#define UI_FTS_STATUS_H

#include "lvgl.h"
#include "eric888_spi_protocol.h"

/**
 * @brief  在指定父容器上创建 FTS 状态面板
 * @param  parent: LVGL 父对象 (如某个 Tab 页或 Panel)
 */
void ui_fts_create(lv_obj_t *parent);

/**
 * @brief  用最新快照数据刷新面板上的所有字段
 * @param  snap: 从 A 板接收的保护状态快照
 * @note   必须在 LVGL 任务上下文中调用 (DisplayTask)
 */
void ui_fts_update(const FTS_StatusSnapshot_t *snap);

#endif /* UI_FTS_STATUS_H */
