# ERIC888 Workspace — 快切装置多板工程

```
ERIC888_Workspace/
│
├── 1_ERIC888_Calc/        ← A板 (计算板)
│   STM32F429, AD7606 FMC 采集, 保护算法, 继电器控制
│
├── 2_ERIC888_Panel/       ← B板 (面板板) [待初始化]
│   LVGL 触摸屏, W5500 以太网, 4G 远程通讯
│
└── 3_Common_Protocol/     ← 跨板共享协议库
    eric888_spi_protocol.h — SPI 帧格式、命令字、数据结构
```

## 编译 A板

```bash
cd 1_ERIC888_Calc
make clean && make
```

## 共享协议使用

两板的 Makefile 均需包含：
```makefile
C_INCLUDES += -I../3_Common_Protocol
```

源码中引用：
```c
#include "eric888_spi_protocol.h"
```

修改协议文件后，**两板需重新编译**以保持一致。
