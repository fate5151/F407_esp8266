# F407_esp8266

STM32F407ZGTx（正点原子探索者 V2.61）上的「3 路继电器 + 触摸屏 + WiFi 远程 + 粉尘监测报警」工程，
CubeMX 生成的 CMake 工程，HAL 库 + arm-none-eabi-gcc，VS Code + Cortex-Debug + pyOCD 烧录。

## 硬件与外设

| 功能 | 外设 | 引脚 | 关键参数 |
|---|---|---|---|
| 3 路继电器 | GPIO 输出 | PC0~PC5 | 低/高电平触发可切换（存 Flash） |
| 蜂鸣器 | GPIO | PC8 | 经 S8050，高电平响 |
| 状态灯 | GPIO | PC9 | 高电平亮 |
| 粉尘 ADC | ADC1_IN4 | PA4 | 12 bit、480 cycles、TIM3_TRGO 下降沿触发 |
| 传感器 LED | TIM3_CH1 | PA6 | 10 ms 周期 / 320 µs 脉冲 |
| 采样点 | TIM3_CH2 | PA7 | 280 µs 处，TRGO = OC2REF |
| 调试串口 | USART1 | PA9 / PA10 | 115200 8N1 |
| WiFi 模块 | USART3 | PB10 / PB11 | 115200 8N1（ATK-MW8266D 插座） |
| 板载 LED | GPIO | PF9 / PF10 | 低电平亮（DS0 心跳 / DS1 网络就绪） |
| **3.5" TFTLCD** | FSMC NE4 | PD/PE/PF12/PG12 + PB15 背光 | NT35310，320×480 竖屏，XPT2046 触摸 |

## 代码结构

```
Core/Src/app_relay.c   3 路继电器位掩码（模式号 0~7）
Core/Src/app_dust.c    ADC 采样 → 9 点中位数 → PM 换算 → 回差报警状态机
Core/Src/app_alarm.c   蜂鸣器 + 状态灯报警指示，ALARM 时自动开排风 CH2
Core/Src/app_net.c     ESP8266（USART3）AT 联网 + HTTP 服务器，浏览器远程控制
Core/Src/app_lcd.c     3.5 寸 TFTLCD 竖屏显示（FSMC NE4，NT35310，320x480）
Core/Src/app_touch.c   XPT2046 电阻触摸 + 4 点校准（结果存 Flash）
Core/Src/app_cfg.c     WiFi 账号掉电保存（Flash sector 10）
Core/Src/lcd.c         正点原子 TFTLCD 驱动 + font.h 字库
Core/Src/delay.c       DWT us 延时 / HAL_Delay ms
Core/Src/app_ui.c      状态显示（暂无屏，先用 USART1 输出一行）
Core/Src/app_cmd.c     USART1 行命令（中断接收 + 环形缓冲）
Core/Src/app_dbg.c     调试串口输出 / printf 重定向
Core/Src/app_util.c    字符串转数字（命令与 URL 参数共用）
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
mode <0-7|0x7>      按位掩码一次性设置 3 路继电器
relay <ch> <0|1>      单路开/关，ch 1~6
toggle <ch>           单路翻转
blink <ch|0>          某路 1 Hz 点动（听声音/量通断），0 = 停
relaypol <0|1>        0=高电平触发，1=低电平触发（存 Flash）
th <ug/m3>            粉尘报警阈值（默认 150）
ui <0|1>              每秒状态行开关
touch <cal|raw|poll|info>  触摸校准 / 读原始值 / 轮询 / 状态
wifi <ssid> <pwd>     连 WiFi 并开 80 端口 HTTP 服务器（存 Flash）
net <stop|status>     停网络 / 看状态和 IP
forget                清除保存的 WiFi 账号
at <AT...>            原始 AT 透传（调试）
```

状态行示例：

```
   12s PM= 35 TH=150 NORMAL RELAY=0x05 [101000] NET=READY 192.168.1.23
```

## 远程控制（ESP8266，T8/T9）

```powershell
# 串口里
wifi MyHome_2.4G 12345678
# → net: joined 'MyHome_2.4G'
# → net: server ready at http://192.168.1.23/
```

手机连同一个 WiFi，浏览器打开该 IP：页面每 3 秒刷新，可翻转 CH1~CH3、
改报警阈值；PM 超阈值页面标红，并自动闭合 CH2（排风），解除后恢复 CH2 原状态。

也可以直接打接口：`/relay?ch=1&on=1`、`/toggle?ch=1`、`/all?m=7`、`/th?v=150`。

> 说明：ESP8266 只支持 2.4 GHz WiFi；AT 固件走 USART3（115200）。

## 进度

- ✅ 外设配置、CMake 工程、业务代码骨架（编译零告警）
- ✅ T7 显示：3.5 寸 TFTLCD（NT35310 / 320×480 竖屏 / FSMC NE4），屏上显示 3 路状态 + PM + 阈值 + 网络
- ✅ 触摸控制：点屏幕方块翻转对应继电器，点 ALL OFF 全断（XPT2046，4 点校准存 Flash）
- ✅ 远程控制：ESP8266 AT 联网 + HTTP 页面控制 3 路继电器 / 阈值（T8/T9）；
  手机实测页面正常、点按钮继电器动作；WiFi 账号掉电保存 + 热点断开自动重连
- ⏳ T1~T6、T9 联动上板实测（逐项验证流程见 [开发指南.md](开发指南.md)）

详细的引脚表、验证步骤、常见坑都在 [开发指南.md](开发指南.md)，
视频演示的功能清单、分镜脚本和操作步骤见 [视频演示说明.md](视频演示说明.md)。
