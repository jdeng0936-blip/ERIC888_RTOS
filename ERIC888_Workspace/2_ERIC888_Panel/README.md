# ERIC888 B板 — 面板板 (Panel Board)

## 职责

- 驱动 LVGL 触摸屏显示实时波形、保护状态
- W5500 以太网通讯（Modbus TCP / IEC 61850）
- 4G 远程遥信遥控
- 作为 SPI Master 与 A板 通信，获取 ADC 数据和系统状态

## 初始化方式

1. 在 STM32CubeMX 中新建项目，芯片选型待定
2. 项目路径选择此目录 (`2_ERIC888_Panel/`)
3. 确保 Makefile 中 `C_INCLUDES` 包含 `-I../3_Common_Protocol`
4. 代码中 `#include "eric888_spi_protocol.h"` 即可使用共享协议
