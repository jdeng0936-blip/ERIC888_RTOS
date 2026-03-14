/**
 * @file    ui_events.c
 * @brief   SquareLine Studio 事件回调实现
 *
 * 硬件关联：
 *   - textarea 输入 → 通过全局变量传给 CommTask → SPI 写 A 板
 *   - switch 切换 → GPIO 直接控制（照明/自检）
 *   - dropdown 选择 → 更新配置参数（存 SD 卡）
 *   - button 点击 → 触发校准/密码验证
 *
 * 注意：这些回调在 LVGL timer_handler 的上下文中执行（displayTask），
 *       不能做耗时操作（如 SD 卡写入），否则 UI 会卡顿。
 *       耗时操作应通过信号量/队列交给其他任务处理。
 */

#include "ui.h"
#include <string.h>
#include <stdlib.h>

/* ================================================================
 * 全局配置参数（供其他模块读取）
 *
 * 这些参数在 UI 回调中被修改，
 * 由 CommTask 定期同步到 A 板（通过 SPI），
 * 由 NetworkTask 定期保存到 SD 卡。
 * ================================================================ */

/* 温度门限 */
static float s_temp_upper_limit = 85.0f;   // ← 上限默认 85℃（电力行业标准）
static float s_temp_lower_limit = 5.0f;    // ← 下限默认 5℃
static float s_humid_upper_limit = 85.0f;  // ← 湿度上限 85% RH
static float s_humid_lower_limit = 30.0f;  // ← 湿度下限 30% RH
static float s_leak_upper_limit = 100.0f;  // ← 泄漏电流上限 100mA
static float s_leak_lower_limit = 10.0f;   // ← 泄漏电流下限 10mA

/* PT/CT 变比（默认值适合 10kV 开关柜） */
static float s_pt_ratio = 100.0f;          // ← PT 变比 10000V:100V = 100
static float s_ct_ratio = 600.0f;          // ← CT 变比 600A:5A = 120（用户可改）

/* 照明/自检状态 */
static uint8_t s_light_on = 0;             // ← 柜内照明灯
static uint8_t s_selftest_on = 0;          // ← 自检模式

/* 柜号 */
static char s_cabinet_id[33] = "0001";     // ← 最多 32 字符

/* ================================================================
 * 回调实现
 * ================================================================ */

/**
 * @brief  textarea 焦点事件 — 弹出/隐藏虚拟键盘
 *
 * 当用户点击输入框（如柜号、IP地址）时，
 * LVGL 会触发 FOCUSED 事件，我们在这里弹出键盘：
 *
 *   手指点击输入框
 *     ↓ FOCUSED 事件
 *   ta_event_cb() 被调用
 *     ↓
 *   弹出键盘（ui_key6 是 SquareLine 里放的键盘控件）
 *     ↓
 *   键盘和 textarea 关联 → 用户可以输入
 */
void ta_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        /* 把键盘关联到当前输入框 */
        if (ui_key6 != NULL) {
            lv_keyboard_set_textarea(ui_key6, ta);
            lv_obj_clear_flag(ui_key6, LV_OBJ_FLAG_HIDDEN);
            // ← 显示键盘（清除 HIDDEN 标志）
        }
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY) {
        /* 输入完成或失去焦点 → 隐藏键盘 */
        if (ui_key6 != NULL) {
            lv_keyboard_set_textarea(ui_key6, NULL);
            lv_obj_add_flag(ui_key6, LV_OBJ_FLAG_HIDDEN);
            // ← 隐藏键盘
        }
    }
}

/**
 * @brief  关闭告警弹窗按钮
 *
 * 硬件关联：无直接硬件操作
 * 功能：隐藏告警面板，用户确认已阅
 */
void aclosebtnclicked(lv_event_t * e)
{
    /* 隐藏告警弹窗 */
    lv_obj_t * target = lv_event_get_target(e);
    lv_obj_t * parent = lv_obj_get_parent(target);
    if (parent != NULL) {
        lv_obj_add_flag(parent, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief  密码验证
 *
 * 正确密码 → 解锁设置页面
 * 错误密码 → 提示错误，不解锁
 *
 * 安全说明：工业设备的密码验证是为了防止误操作，
 *           不是为了安全加密。密码通常是出厂固定的。
 */
void checkpassword(lv_event_t * e)
{
    /* 获取密码输入框的文本 */
    const char *input = lv_textarea_get_text(ui_yeartextarea12);
    // ← ui_yeartextarea12 是密码输入框（在 SquareLine 中设计的）

    /* 默认密码：0000（电力行业常用出厂密码） */
    if (strcmp(input, "0000") == 0) {
        /* 密码正确 → 隐藏密码面板 */
        lv_obj_t * parent = lv_event_get_target(e);
        lv_obj_t * panel = lv_obj_get_parent(lv_obj_get_parent(parent));
        if (panel != NULL) {
            lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        /* 密码错误 → 清空输入框，提示重试 */
        lv_textarea_set_text(ui_yeartextarea12, "");
        lv_textarea_set_placeholder_text(ui_yeartextarea12, "密码错误");
        // ← 用 placeholder 文字提示
    }
}

/**
 * @brief  自检开关切换
 *
 * 硬件关联：
 *   自检模式开启时 → 通过 SPI 告诉 A 板进入自检
 *   A 板会依次触发各保护功能，验证跳闸回路
 *
 *   控制链路：
 *   Switch → s_selftest_on → CommTask → SPI → A板 → 逐项自检
 */
void selftestvaluechanged(lv_event_t * e)
{
    lv_obj_t * sw = lv_event_get_target(e);
    s_selftest_on = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0;
    // ← LV_STATE_CHECKED = 开关拨到 ON 位置
    // ← s_selftest_on 会被 CommTask 读取并发送给 A 板
}

/**
 * @brief  柜内照明开关切换
 *
 * 硬件关联：
 *   PH10 → LED1（运行指示灯，已用）
 *   照明灯控制通过继电器：
 *     B 板 → SPI 命令 → A 板 → GPIO 驱动继电器 → 照明灯
 *
 *   或者 B 板直接有一个 GPIO 控制照明继电器：
 *     Switch ON → HAL_GPIO_WritePin(LIGHT_GPIO, LIGHT_PIN, SET)
 */
void lightvaluechanged(lv_event_t * e)
{
    lv_obj_t * sw = lv_event_get_target(e);
    s_light_on = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0;

    /* 直接控制照明 GPIO（如果 B 板有直接控制权） */
    /* HAL_GPIO_WritePin(GPIOX, GPIO_PIN_X, s_light_on ? GPIO_PIN_SET : GPIO_PIN_RESET); */

    /* 或通过 SPI 告诉 A 板控制继电器 */
    /* 由 CommTask 在下一个 SPI 周期发送 */
}

/**
 * @brief  历史记录标签页切换
 *
 * 当用户在"查询"标签页中切换子标签（温度/电流/电压/故障）时，
 * 需要从 SD 卡加载对应的历史数据并显示。
 */
void histtablechanged(lv_event_t * e)
{
    /* 获取当前选中的子标签索引 */
    lv_obj_t * tabview = lv_event_get_target(e);
    uint16_t tab_idx = lv_tabview_get_tab_act(tabview);
    // ← 0=温度曲线, 1=电流曲线, 2=电压曲线, 3=故障记录
    (void)tab_idx;
    /* TODO: 根据 tab_idx 从 SD 卡加载历史数据 */
}

/**
 * @brief  触头类型下拉框选择
 *
 * 选择触头监测类型（上触头/下触头/电缆头）
 */
void chutouvaluechanged(lv_event_t * e)
{
    lv_obj_t * dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    (void)sel;
    /* 0=上触头, 1=下触头, 2=电缆头 */
    /* 切换当前监测显示的数据源 */
}

/**
 * @brief  校准准备按钮
 *
 * 进入校准模式前的准备：
 *   1. 提示用户确认（校准会改变测量参数）
 *   2. 锁定其他操作
 */
void preparecalcclicked(lv_event_t * e)
{
    (void)e;
    /* TODO: 显示校准准备对话框 */
}

/**
 * @brief  电压校准按钮
 *
 * 硬件关联：
 *   用户输入标准电压值 → SPI 发给 A 板 → A 板计算校准系数
 *   → 存入 Flash → 后续测量自动修正
 */
void voltagecalcclicked(lv_event_t * e)
{
    (void)e;
    /* TODO: 读取用户输入的标准值 → 发 SPI 校准命令 */
}

/**
 * @brief  电流校准按钮（同上）
 */
void currentcalcclicked(lv_event_t * e)
{
    (void)e;
    /* TODO: 读取用户输入的标准值 → 发 SPI 校准命令 */
}

/**
 * @brief  触摸屏校准
 *
 * 硬件关联：
 *   屏幕上显示 3 个校准点 → 用户依次点击
 *   → 计算仿射变换矩阵 → 存入 Flash
 *   → 之后每次触摸读数都经过矩阵修正
 */
void touchscreencalc(lv_event_t * e)
{
    (void)e;
    /* TODO: 启动 3 点触摸校准流程 */
}

/**
 * @brief  单元格数据标签页切换
 */
void celldatatablechanged(lv_event_t * e)
{
    lv_obj_t * tabview = lv_event_get_target(e);
    uint16_t tab_idx = lv_tabview_get_tab_act(tabview);
    (void)tab_idx;
    /* 切换不同的数据显示面板 */
}

/**
 * @brief  清除日志按钮
 *
 * 功能：清空 SD 卡上的故障历史记录
 * 安全：需要密码验证后才能操作
 */
void clearlogclicked(lv_event_t * e)
{
    (void)e;
    /* TODO: 删除 SD 卡上的 logs/ 目录内容 */
    /* f_unlink("logs/fault.log"); */
}

/**
 * @brief  A 相位置下拉框选择
 *
 * 功能：配置无线测温传感器的安装位置
 * 选项：上触头 / 下触头 / 电缆头
 */
void positionAchanged(lv_event_t * e)
{
    lv_obj_t * dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    (void)sel;
    /* 保存 A 相传感器安装位置 */
}

/**
 * @brief  A 相传感器类型选择
 */
void typeAchanged(lv_event_t * e)
{
    lv_obj_t * dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    (void)sel;
}

void typeBchanged(lv_event_t * e)
{
    lv_obj_t * dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    (void)sel;
}

void positionBchanged(lv_event_t * e)
{
    lv_obj_t * dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    (void)sel;
}

void typeCchanged(lv_event_t * e)
{
    lv_obj_t * dd = lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    (void)sel;
}

/**
 * @brief  通讯类型切换（RS485 / 以太网 / 4G）
 */
void connecttypechanged(lv_event_t * e)
{
    lv_obj_t * cb = lv_event_get_target(e);
    uint8_t checked = lv_obj_has_state(cb, LV_STATE_CHECKED) ? 1 : 0;
    (void)checked;
    /* TODO: 根据勾选状态启用/禁用对应通讯模块 */
}

/**
 * @brief  设置页标签页切换
 *
 * 切换设置子页面（温度设置 / 变比设置 / 通讯设置 / 柜号设置 / 时间设置）
 */
void settingstablechanged(lv_event_t * e)
{
    lv_obj_t * tabview = lv_event_get_target(e);
    uint16_t tab_idx = lv_tabview_get_tab_act(tabview);
    (void)tab_idx;
}

/**
 * @brief  主标签页切换
 *
 * 切换主页面的 6 个标签（实时监控 / 无线测温 / 曲线 / 查询 / 设置 / 关于）
 */
void maintablechanged(lv_event_t * e)
{
    lv_obj_t * tabview = lv_event_get_target(e);
    uint16_t tab_idx = lv_tabview_get_tab_act(tabview);
    (void)tab_idx;
    /* 可用于按需加载数据，减少不可见页面的刷新开销 */
}

/**
 * @brief  A 相温度参数输入完成
 *
 * 用户在 textarea 中输入了温度门限值 → 保存到全局变量
 */
void tempAdefocused(lv_event_t * e)
{
    lv_obj_t * ta = lv_event_get_target(e);
    const char * text = lv_textarea_get_text(ta);
    if (text && strlen(text) > 0) {
        float val = (float)atof(text);
        if (val > 0 && val < 200) {
            s_temp_upper_limit = val;
            // ← 更新温度上限（自动在下一个 SPI 周期同步到 A 板）
        }
    }
}

/**
 * @brief  A 相传感器 ID 输入完成
 */
void idAdefocused(lv_event_t * e)
{
    (void)e;
    /* TODO: 保存无线测温传感器的 ID 到配置 */
}

void tempBdefocused(lv_event_t * e)
{
    lv_obj_t * ta = lv_event_get_target(e);
    const char * text = lv_textarea_get_text(ta);
    if (text && strlen(text) > 0) {
        float val = (float)atof(text);
        if (val > 0 && val < 200) {
            s_temp_lower_limit = val;
        }
    }
}

void idBdefocused(lv_event_t * e)
{
    (void)e;
}

void tempCdefocused(lv_event_t * e)
{
    (void)e;
    /* 预留：C 相温度参数 */
}

void idCdefocused(lv_event_t * e)
{
    (void)e;
}
