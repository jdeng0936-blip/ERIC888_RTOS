#!/bin/bash
# ============================================================
# ERIC888 B-board (STM32F429) J-Link 烧录脚本
#
# 硬件接线：
#   J-Link 调试器 → B 板 SWD 接口
#   ┌──────────┐        ┌──────────┐
#   │ J-Link   │        │ B 板 SWD │
#   │  VTref ──┼── 3.3V ┤ VCC      │  ← J-Link 检测目标电压
#   │  SWDIO ──┼────────┤ SWDIO    │  ← 数据线
#   │  SWCLK ──┼────────┤ SWCLK    │  ← 时钟线
#   │  GND   ──┼────────┤ GND      │  ← 地线
#   └──────────┘        └──────────┘
#
# 用法：./flash_jlink.sh
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ELF_FILE="$SCRIPT_DIR/build/ERIC888_Panel.elf"

# 检查 ELF 文件是否存在
if [ ! -f "$ELF_FILE" ]; then
    echo "❌ 找不到固件文件: $ELF_FILE"
    echo "   请先运行 make 编译"
    exit 1
fi

echo "📦 固件: $ELF_FILE"
echo "📏 大小: $(arm-none-eabi-size "$ELF_FILE" | tail -1 | awk '{print $1/1024 "KB Flash, " $3/1024 "KB RAM"}')"
echo ""
echo "🔌 正在连接 J-Link → STM32F429..."
echo ""

# 生成 J-Link 命令脚本
JLINK_SCRIPT="/tmp/jlink_flash.jlink"
cat > "$JLINK_SCRIPT" << EOF
si SWD
speed 4000
device STM32F429ZI
connect
r
h
loadfile $ELF_FILE
r
g
exit
EOF

# ← si SWD        = 使用 SWD 接口（不是 JTAG）
# ← speed 4000    = 4MHz 时钟（稳定又快）
# ← device STM32F429ZI = 我们的芯片型号
# ← connect       = 连接目标芯片
# ← r             = 复位芯片
# ← h             = 暂停 CPU
# ← loadfile      = 把 .elf 写入 Flash
# ← r             = 再次复位
# ← g             = 启动运行（go）

JLinkExe -AutoConnect 1 -ExitOnError 1 -CommandFile "$JLINK_SCRIPT"

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ 烧录成功！B 板已开始运行"
    echo "   👀 看屏幕 → 应该出现启动画面 → 主界面"
else
    echo ""
    echo "❌ 烧录失败！检查："
    echo "   1. J-Link USB 线连好了吗？"
    echo "   2. B 板通电了吗？"
    echo "   3. SWD 4 根线 (VCC/SWDIO/SWCLK/GND) 接对了吗？"
fi
