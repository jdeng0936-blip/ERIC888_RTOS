/**
 * @file   ui_fts_status.c
 * @brief  FTS 保护状态 UI 面板实现 — LVGL 8.x
 *
 * ✅ [Embedded-Engineer]
 * 布局 (纵向排列)：
 *   ┌───────────────────────────────────────┐
 *   │ 🔌 保护状态: [NORMAL/TRIPPED/LOCKOUT] │  ← 状态 LED + 文本
 *   │ ⚡ 故障类型: [具体中文描述]            │
 *   │ 🔀 运行拓扑: [SPLIT_BUS / ...]        │
 *   │ 📐 主备相差: [xx.x°]                  │
 *   │ 🔋 备用电源: [可用 / 不可用]           │
 *   │ 📼 录波状态: [运行 / 已冻结]           │
 *   │ ⏱ 运行时间: [xx:xx:xx]                │
 *   └───────────────────────────────────────┘
 *
 * LOCKOUT 时叠加全屏红色告警弹窗 (模态)
 */
#include "ui_fts_status.h"
#include <stdio.h>

/* ═══════════ 私有 UI 对象 ═══════════ */
static lv_obj_t *s_panel;          /* 面板容器 */
static lv_obj_t *s_led_state;      /* 状态 LED */
static lv_obj_t *s_lbl_state;      /* 保护状态文本 */
static lv_obj_t *s_lbl_fault;      /* 故障类型 */
static lv_obj_t *s_lbl_topo;       /* 运行拓扑 */
static lv_obj_t *s_lbl_phase;      /* 相角差 */
static lv_obj_t *s_lbl_backup;     /* 备用电源 */
static lv_obj_t *s_lbl_recorder;   /* 录波状态 */
static lv_obj_t *s_lbl_uptime;     /* 运行时间 */
static lv_obj_t *s_msgbox_lockout; /* LOCKOUT 告警弹窗 */
static uint8_t   s_lockout_shown;  /* 防止重复弹窗 */

/* ═══════════ 枚举 → 中文映射 ═══════════ */

static const char* fts_state_name(uint8_t state)
{
    switch (state) {
        case FTS_STATE_INIT:      return "初始化";
        case FTS_STATE_NORMAL:    return "正常运行";
        case FTS_STATE_TRIPPED:   return "保护动作";
        case FTS_STATE_LOCKOUT:   return "闭锁锁定";
        default:                  return "未知";
    }
}

static lv_color_t fts_state_color(uint8_t state)
{
    switch (state) {
        case FTS_STATE_NORMAL:    return lv_color_hex(0x00C853); /* 绿色 */
        case FTS_STATE_TRIPPED:   return lv_color_hex(0xFFAB00); /* 琥珀色 */
        case FTS_STATE_LOCKOUT:   return lv_color_hex(0xD50000); /* 红色 */
        default:                  return lv_color_hex(0x9E9E9E); /* 灰色 */
    }
}

static const char* fault_type_name(uint8_t fault)
{
    switch (fault) {
        case FAULT_NONE:          return "无故障";
        case FAULT_FEEDER1:       return "进线1故障";
        case FAULT_FEEDER2:       return "进线2故障";
        case FAULT_BUS:           return "母线故障";
        case FAULT_PT_BROKEN:     return "PT断线";
        default:                  return "未知故障";
    }
}

static const char* topology_name(uint8_t topo)
{
    switch (topo) {
        case TOPO_UNKNOWN:        return "未知";
        case TOPO_SPLIT_SRC1:     return "分段-电源1带";
        case TOPO_SPLIT_SRC2:     return "分段-电源2带";
        case TOPO_PARALLEL:       return "并列运行";
        case TOPO_MAINTENANCE:    return "检修停电";
        case TOPO_ERROR:          return "DI矛盾";
        default:                  return "未知拓扑";
    }
}

/* ═══════════ 辅助：创建一行 "标题: 值" ═══════════ */

static lv_obj_t* create_row(lv_obj_t *parent, const char *title, lv_obj_t **out_value)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 标题标签 (固定宽度) */
    lv_obj_t *lbl_title = lv_label_create(row);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_width(lbl_title, 120);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xB0BEC5), 0);

    /* 值标签 */
    lv_obj_t *lbl_val = lv_label_create(row);
    lv_label_set_text(lbl_val, "--");
    lv_obj_set_style_text_color(lbl_val, lv_color_hex(0xFFFFFF), 0);

    *out_value = lbl_val;
    return row;
}

/* ═══════════ 公开接口 ═══════════ */

void ui_fts_create(lv_obj_t *parent)
{
    /* 主面板容器 */
    s_panel = lv_obj_create(parent);
    lv_obj_set_size(s_panel, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0x1A237E), 0); /* 深蓝底 */
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(0x3949AB), 0);
    lv_obj_set_style_border_width(s_panel, 2, 0);
    lv_obj_set_style_radius(s_panel, 8, 0);
    lv_obj_set_style_pad_all(s_panel, 8, 0);
    lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* ── 标题行：LED + 状态文本 ── */
    lv_obj_t *header = lv_obj_create(s_panel);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 4, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_led_state = lv_led_create(header);
    lv_led_set_color(s_led_state, lv_color_hex(0x9E9E9E));
    lv_obj_set_size(s_led_state, 16, 16);
    lv_led_on(s_led_state);

    lv_obj_t *lbl_header = lv_label_create(header);
    lv_label_set_text(lbl_header, "保护状态:");
    lv_obj_set_style_text_color(lbl_header, lv_color_hex(0xB0BEC5), 0);
    lv_obj_set_style_pad_left(lbl_header, 8, 0);

    s_lbl_state = lv_label_create(header);
    lv_label_set_text(s_lbl_state, "--");
    lv_obj_set_style_text_color(s_lbl_state, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_left(s_lbl_state, 4, 0);

    /* ── 数据行 ── */
    create_row(s_panel, "故障类型:", &s_lbl_fault);
    create_row(s_panel, "运行拓扑:", &s_lbl_topo);
    create_row(s_panel, "主备相差:", &s_lbl_phase);
    create_row(s_panel, "备用电源:", &s_lbl_backup);
    create_row(s_panel, "录波状态:", &s_lbl_recorder);
    create_row(s_panel, "运行时间:", &s_lbl_uptime);

    s_msgbox_lockout = NULL;
    s_lockout_shown = 0;
}

void ui_fts_update(const FTS_StatusSnapshot_t *snap)
{
    if (snap == NULL || s_panel == NULL) return;

    char buf[32];

    /* ── 保护状态 + LED ── */
    lv_label_set_text(s_lbl_state, fts_state_name(snap->fts_state));
    lv_led_set_color(s_led_state, fts_state_color(snap->fts_state));

    /* ── 故障类型 ── */
    lv_label_set_text(s_lbl_fault, fault_type_name(snap->fault_type));
    if (snap->fault_type != FAULT_NONE) {
        lv_obj_set_style_text_color(s_lbl_fault, lv_color_hex(0xFF5252), 0);
    } else {
        lv_obj_set_style_text_color(s_lbl_fault, lv_color_hex(0x69F0AE), 0);
    }

    /* ── 运行拓扑 ── */
    lv_label_set_text(s_lbl_topo, topology_name(snap->topology));

    /* ── 主备相角差 ── */
    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0",
             (double)(snap->phase_diff_deg10 / 10.0f)); /* ×10 → 度 */
    lv_label_set_text(s_lbl_phase, buf);

    /* 相差超阈值时变红 (>15°) */
    if (snap->phase_diff_deg10 > 150 || snap->phase_diff_deg10 < -150) {
        lv_obj_set_style_text_color(s_lbl_phase, lv_color_hex(0xFF5252), 0);
    } else {
        lv_obj_set_style_text_color(s_lbl_phase, lv_color_hex(0x69F0AE), 0);
    }

    /* ── 备用电源 ── */
    if (snap->backup_available) {
        lv_label_set_text(s_lbl_backup, "可用");
        lv_obj_set_style_text_color(s_lbl_backup, lv_color_hex(0x69F0AE), 0);
    } else {
        lv_label_set_text(s_lbl_backup, "不可用");
        lv_obj_set_style_text_color(s_lbl_backup, lv_color_hex(0xFF5252), 0);
    }

    /* ── 录波状态 ── */
    if (snap->recorder_frozen) {
        lv_label_set_text(s_lbl_recorder, "已冻结 (有故障录波)");
        lv_obj_set_style_text_color(s_lbl_recorder, lv_color_hex(0xFFAB00), 0);
    } else {
        lv_label_set_text(s_lbl_recorder, "运行中");
        lv_obj_set_style_text_color(s_lbl_recorder, lv_color_hex(0x69F0AE), 0);
    }

    /* ── 运行时间 (uptime_ms → HH:MM:SS) ── */
    {
        uint32_t sec = snap->uptime_ms / 1000;
        uint32_t h = sec / 3600;
        uint32_t m = (sec % 3600) / 60;
        uint32_t s = sec % 60;
        snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                 (unsigned long)h, (unsigned long)m, (unsigned long)s);
        lv_label_set_text(s_lbl_uptime, buf);
    }

    /* ═══════════ LOCKOUT 全屏红色告警 ═══════════ */
    if (snap->fts_state == FTS_STATE_LOCKOUT && !s_lockout_shown) {
        /*
         * ✅ [Embedded-Engineer] 模态告警弹窗
         *
         * LOCKOUT = 系统已三击闭锁，不可自动恢复。
         * 必须让运维人员在面板上第一时间看到！
         * 红色全屏弹窗 + 无关闭按钮 → 只能通过"复位"操作解除。
         */
        static const char *btns[] = {"", ""};
        s_msgbox_lockout = lv_msgbox_create(NULL, "⚠ 系统闭锁",
            "保护系统已闭锁！\n需人工排查故障后手动复位。",
            btns, false);
        lv_obj_set_style_bg_color(s_msgbox_lockout, lv_color_hex(0xB71C1C), 0);
        lv_obj_set_style_bg_opa(s_msgbox_lockout, LV_OPA_90, 0);
        lv_obj_set_style_text_color(s_msgbox_lockout, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_size(s_msgbox_lockout, 400, 200);
        lv_obj_center(s_msgbox_lockout);
        s_lockout_shown = 1;
    }

    /* LOCKOUT 解除后关闭弹窗 */
    if (snap->fts_state != FTS_STATE_LOCKOUT && s_lockout_shown) {
        if (s_msgbox_lockout != NULL) {
            lv_msgbox_close(s_msgbox_lockout);
            s_msgbox_lockout = NULL;
        }
        s_lockout_shown = 0;
    }
}
