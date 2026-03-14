#!/usr/bin/env python3
"""
生成 GB2312 一级汉字列表（3755个常用汉字）
GB2312 一级字区：0xB0A1 ~ 0xD7F9
覆盖范围：所有常用汉字 + 省市县地名用字
"""
import struct

chars = []

# GB2312 一级汉字区：区码 16~55 (0xB0~0xD7)
for qu in range(0xB0, 0xD7 + 1):
    for wei in range(0xA1, 0xFE + 1):
        # 最后一区 0xD7 只到 0xF9
        if qu == 0xD7 and wei > 0xF9:
            break
        gb_bytes = bytes([qu, wei])
        try:
            char = gb_bytes.decode('gb2312')
            chars.append(char)
        except:
            pass

# 输出为一行字符串（供 lv_font_conv --symbols 使用）
result = ''.join(chars)
print(f"Total: {len(chars)} characters")

with open('/Users/imac2026/Desktop/ERIC888_RTOS/ERIC888_Workspace/2_ERIC888_Panel/ERIC888_Panel/gb2312_chars.txt', 'w', encoding='utf-8') as f:
    f.write(result)

print("Written to gb2312_chars.txt")
