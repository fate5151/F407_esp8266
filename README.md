# F407_esp8266

STM32F407ZGTx（正点原子探索者 V2.61）上的「6 路继电器 + 2 路调光 + 粉尘监测报警」工程，
CubeMX 生成的 CMake 工程，HAL 库 + arm-none-eabi-gcc，VS Code + Cortex-Debug + pyOCD 烧录。

## 硬件与外设

| 功能 | 外设 | 引脚 | 关键参数 |
|---|---|---|---|
| 6 路继电器 | GPIO 输出 | PC0~PC5 | 高电平吸合 |
| 2 路调光 | TIM8_CH1/CH2 | PC6 / PC7 | 1 kHz（PSC 83、ARR 999） |
| 蜂鸣器 | GPIO | PC8 | 经 S8050，高电平响 |
| 状态灯 | GPIO | PC9 | 高电平亮 |
| 粉尘 ADC | ADC1_IN0 | PA0 | 12 bit、480 cycles、TIM3_TRGO 下降沿触发 |
| 传感器 LED | TIM3_CH1 | PA6 | 10 ms 周期 / 320 µs 脉冲 |
| 采样点 | TIM3_CH2 | PA7 | 280 µs 处，TRGO = OC2REF |
| 调试串口 | USART1 | PA9 / PA10 | 115200 8N1 |
| WiFi 模块 | USART3 | PB10 / PB11 | 115200 8N1（ATK-MW8266D 插座） |
| 板载 LED | GPIO | PF9 / PF10 | 低电平亮 |

## 代码结构

```
Core/Src/app_relay.c   6 路继电器位掩码（模式号 0~63）
Core/Src/app_dim.c     TIM8 两路调光 0~100%
Core/Src/app_dust.c    ADC 采样 → 9 点中位数 → PM 换算 → 回差报警状态机
Core/Src/app_alarm.c   蜂鸣器 + 状态灯报警指示
Core/Src/app_ui.c      状态显示（暂无屏，先用 USART1 输出一行）
Core/Src/app_cmd.c     USART1 行命令（中断接收 + 环形缓冲）
Core/Src/app_dbg.c     调试串口输出 / printf 重定向
```

主循环是 10 ms / 1 s 两级时间片，不用 RTOS、不动态分配内存。

粉尘换算：`PM = (Vo_mV − 900) × 0.2`，Vo 由 ADC 按 0.5 分压还原。

## 编译

需要 `cmake ≥ 3.22`、`ninja`、`arm-none-eabi-gcc 14.2`。

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

产物：`build/Debug/F407_esp8266.elf`（当前 FLASH 25 696 B / 2.45%，RAM 3 536 B / 2.70%）。

## 烧录

VS Code 里 `Ctrl+Shift+B` 会先编译再调用 pyOCD 烧录（见 `.vscode/tasks.json`），
前提是 `pyocd 0.39.0` 且环境变量 `PYTHONUTF8=1`，配置见 [pyocd.yaml](pyocd.yaml)。

## 串口命令（USART1，115200 8N1，带回显）

```
help                  列出命令
status                立刻打印一行状态
mode <0-63|0x3F>      按位掩码一次性设置 6 路继电器
relay <ch> <0|1>      单路开/关，ch 1~6
toggle <ch>           单路翻转
dim <ch> <0-100>      调光占空比，ch 1~2
th <ug/m3>            粉尘报警阈值（默认 150）
ui <0|1>              每秒状态行开关
```

状态行示例：

```
    12s  PM= 35 ug/m3  TH=150  LV=WARN    RELAY=0x05 [101000]  DIM= 50%/  0%
```

## 进度

- ✅ 外设配置、CMake 工程、业务代码骨架（编译零告警）
- ⏳ T1~T10 上板实测（逐项验证流程见 [开发指南.md](开发指南.md)）
- ⏳ ESP8266 联网部分（USART3 + AT + HTTP）尚未实现

详细的引脚表、验证步骤、常见坑都在 [开发指南.md](开发指南.md)。
