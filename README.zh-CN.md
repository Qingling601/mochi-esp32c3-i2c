# Mochi ESP32-C3 I2C OLED

[English](README.md) | **简体中文**

在 **ESP32-C3 SuperMini** + **4 针 I2C OLED（SSD1306 128x64）** 上播放 Dasai Mochi 表情动画，支持触摸"摸头"交互、震动反馈，并内置两个单键小游戏。

> 本项目是 [huykhoong/esp32_dasai_mochi_clone_and_how_to](https://github.com/huykhoong/esp32_dasai_mochi_clone_and_how_to) 的衍生移植版本。原项目使用 U8g2 库驱动 TFT/OLED，并提供了通过 [gif2cpp](https://huykhong.com/IOT/gif2cpp) 将 GIF 转成 C++ 数组的思路与素材。本仓库在原思路基础上改为驱动 4 针 I2C OLED，并扩展了触摸交互与游戏功能。**原始创意、素材与 GIF 转换方案归原作者所有，特此致谢。**

---

## 功能特性

- **表情动画播放**：将 GIF 逐帧转为 1 bit/像素数据，存于 flash 中播放。
- **触摸"摸头"交互**：手指触摸 TTP223 模块时播放"笑"的表情并触发震动马达；松开立即停止（逐帧中断，响应快）。
- **随机表情**：不触摸时以"生气"为主，约 10 次里有 1 次"害羞"、1 次"得意"。
- **震动反馈**：PWM 控制震动强度（300Hz 低频，感性负载才有持续扭力）。
- **游戏菜单**：长按触摸 3 秒进入菜单，内含两个单键小游戏：
  - **FLAPPY BIRD（你能飞多远）**：按住上升、松开下降，穿过管道缺口得分。
  - **JUMP JUMP（跳一跳）**：按住蓄力、松开弹跳，落到下一方块得分。

---

## 硬件清单

| 元件 | 说明 |
|------|------|
| ESP32-C3 SuperMini | 主控 |
| 0.96" OLED 4 针 I2C 模块 | SSD1306，128x64，I2C 地址 `0x3C` |
| TTP223 电容触摸模块 | 点动模式（默认），高电平触发 |
| 震动马达 + MOS 驱动板 | 高电平触发、低电平截止 |
| 面包板 + 杜邦线 | 若干 |

---

## 接线

### 4 针 OLED（I2C）

```
4针 OLED            ESP32-C3 SuperMini
──────────────────────────────────────
GND              →  GND
VCC              →  3.3V
SCL              →  GPIO6
SDA              →  GPIO7
```

### TTP223 触摸模块

```
TTP223             ESP32-C3 SuperMini
──────────────────────────────────────
VCC              →  3.3V
GND              →  GND
I/O (输出)        →  GPIO1
```

### 震动马达（带 MOS 驱动板）

```
马达驱动板          ESP32-C3 SuperMini
──────────────────────────────────────
VCC              →  5V
GND              →  GND
IN (控制脚)       →  GPIO3
```

> **务必共地**：OLED、触摸模块、马达驱动板的 GND 都要和 ESP32 连在一起。
> 4 针 I2C 模块没有 RES/DC/CS 引脚，无需连接。

---

## 编译与烧录

需要 [PlatformIO](https://platformio.org/)（VSCode 插件或命令行均可）。首次编译会自动下载 `espressif32` 平台与工具链。

```bash
# 编译 + 烧录主固件
pio run -e esp32c3 -t upload

# 打开串口监视器
pio device monitor -b 115200
```

如果 `pio` 不在 PATH 中（PlatformIO 装在 `~/.platformio`），使用完整路径：

```bash
~/.platformio/penv/bin/pio run -e esp32c3 -t upload
```

### I2C 地址扫描诊断（可选）

若屏幕不亮，可先烧录扫描程序确认 OLED 是否在线及其地址：

```bash
~/.platformio/penv/bin/pio run -e scan -t upload
~/.platformio/penv/bin/pio device monitor -b 115200
```

正常应看到 `FOUND device at 0x3C`。若无设备，检查 SCL/SDA 接线与供电。

---

## 操作说明

| 状态 | 操作 | 效果 |
|------|------|------|
| 待机 | 不触摸 | 播放生气表情（约 1/10 害羞、1/10 得意） |
| 待机 | 短按触摸 | 播"笑"+ 震动（松开即停） |
| 待机 | **长按 3 秒** | 进入游戏菜单 |
| 菜单 | 短按 | 光标移到下一个游戏 |
| 菜单 | 长按 2 秒 | 进入当前高亮的游戏 |
| Flappy | 按住 / 松开 | 鸟上升 / 下降，穿越缺口得分 |
| 跳一跳 | 按住 / 松开 | 蓄力 / 起跳，落到下一方块得分 |
| 游戏结束 | 短按 | 重玩 |
| 游戏结束 | 长按 3 秒 | 返回待机（表情）模式 |

---

## 可调参数

均在 [`src/main.cpp`](src/main.cpp) 顶部 `#define`：

**Flappy Bird**
```c
#define PIPE_GAP     24   // 管道缺口高度（越小越难）
#define PIPE_SPACING 52   // 管道间距（越小越密）
#define FRAME_MS     32   // 每帧毫秒（越小越快）
```

**跳一跳**
```c
#define JUMP_GAP_MIN  8   // 方块最小间距
#define JUMP_GAP_VAR  6   // 间距随机幅度
#define JUMP_MAX_CHARGE 20 // 蓄力上限
```

**震动强度**：`setVibrate(45)` 中的 `45` 为占空比百分比（0~100，越小越弱）。

---

## 一些实现要点

- **硬件 I2C**：使用 Arduino `Wire` 库，`Wire.begin(SDA, SCL)` + 400kHz。**注意不要在 `Wire.begin()` 之后再对 SCL/SDA 调 `pinMode()`**，否则会覆盖 I2C 外设配置导致屏幕不亮。
- **整页刷新**：`OLED_Refresh()` 每页用一个 I2C 事务发送 `0x40 + 128 字节`（`Wire.setBufferSize(160)` 保证不溢出），把 8 页从 32 次事务降到 8 次，显著提速。
- **GIF 数据格式**：1 bit/像素、行优先、字节内 MSB 在前。每帧固定 1024 字节（128 × 64 / 8）。数组为 `const`（存 flash），不要去掉 `const`。
- **`ANIMATED_GIF_DEFINED` 守卫**：每个 GIF 头文件都有此守卫，使多个 GIF 头可同时 include。请勿删除。

---

## 目录结构

```
esp32c3_4pin_oled/
├── platformio.ini          # PlatformIO 配置（主固件 + scan 诊断两个环境）
├── src/
│   ├── main.cpp            # 主固件（显示驱动 + 动画 + 触摸 + 游戏）
│   └── scan.cpp            # I2C 地址扫描诊断程序
└── include/
    ├── font.h              # ASCII / 中文字库
    ├── angry.h             # 表情动画数据（GIF 转 C++）
    ├── laugh.h
    ├── relaxed.h
    ├── embarrassed.h
    ├── proud.h
    ├── daichi_gundam.h     # 来自原开源项目的动画
    └── daichi_intro.h
```

---

## 致谢与许可

- **原始项目**：[huykhoong/esp32_dasai_mochi_clone_and_how_to](https://github.com/huykhoong/esp32_dasai_mochi_clone_and_how_to)
- **GIF 转 C++ 工具**：[gif2cpp](https://huykhong.com/IOT/gif2cpp)
- **角色版权**：Dasai Mochi 形象版权归其原作者所有，本项目仅供学习交流使用。
- **OLED 驱动参考**：中景园电子（SSD1306 驱动与字库）。
