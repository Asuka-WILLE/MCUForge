# UM10550 轮毂电机与升降机构遥控控制程序

本工程是基于 STM32H723VETx 的 CubeMX/MDK 工程，用 HT-10A 十通道遥控器的 SBUS 接收信号控制两侧和利时 UM 系列伺服轮毂一体机，以及一路 RS485 升降机构。

当前代码的核心控制链路是：

```text
HT-10A 遥控器
  -> SBUS 接收机
  -> UART5 中断接收 SBUS 帧
  -> 解析 CH3 / CH4 / CH6 / CH8
  -> USART2 RS485 控制左右轮毂电机
  -> USART3 RS485 控制升降机构
  -> USB CDC 虚拟串口向电脑发送实时遥测数据
  -> Python/Tkinter 上位机实时显示数值和速度曲线
```

## Clone 后打开工程

Clone 本仓库后，不需要额外复制工程文件，可以直接从下面两个入口打开并修改：

- CubeMX：打开根目录下的 `UM10550.ioc`，修改引脚、外设或时钟配置后重新生成代码。
- Keil MDK：打开 `MDK-ARM/UM10550.uvprojx`，即可查看、修改和编译工程代码。

已纳入版本管理的关键工程内容包括 `UM10550.ioc`、`.mxproject`、`Core/`、`Drivers/`、`MDK-ARM/UM10550.uvprojx`、`MDK-ARM/startup_stm32h723xx.s` 和 `MDK-ARM/RTE/`。这些文件足够支撑别人 clone 后继续用 CubeMX 或 Keil 修改工程。

本工程的 CubeMX 配置使用 `STM32Cube FW_H7 V1.13.0`，目标工具链为 `MDK-ARM V5.32` 工程格式；本地验证使用 Keil MDK `V5.40` 与 Arm Compiler `6.22`。Keil 工程引用了 `Keil.STM32H7xx_DFP.4.1.3` 和 ARM CMSIS Pack；如果别人打开时提示缺少 Pack，在 Keil Pack Installer 中安装对应 STM32H7xx DFP / CMSIS 包即可。

未提交的 `MDK-ARM/UM10550/`、`Objects/`、`Listings/`、`*.uvoptx`、`*.uvguix.*`、`*.dbgconf` 等文件属于编译产物或本机 Keil 调试/界面设置，Keil 打开或编译后会自动生成，不影响继续修改工程。

## 资料来源

本 README 结合了当前代码和以下本地资料：

- `C:\Users\hz_wu\Desktop\轮式人形资料\MC02开发板`
- `C:\Users\hz_wu\Desktop\轮式人形资料\2.4G 十通道遥控器资料`
- `C:\Users\hz_wu\Desktop\轮式人形资料\轮毂电机\和利时电机资料`
- `C:\Users\hz_wu\Desktop\轮式人形资料\升降机构\升降机构使用手册.pdf`

其中，UM 系列伺服轮毂一体机资料说明该电机将轮毂电机与 DS 系列伺服驱动器合为一体，支持 RS485/Modbus 通讯控制。本工程使用的站号为左轮 `1`、右轮 `2`。

## 当前实现的功能

1. 遥控器 SBUS 接收与解析。
2. 左摇杆上下通道 `CH3` 控制前进/后退速度。
3. 左摇杆左右通道 `CH4` 控制左右转向。
4. 通过差速计算生成左右轮目标转速。
5. 使用 USART2 的 RS485 总线向左右 UM 一体式轮毂电机发送速度指令。
6. `CH6` 作为急停/使能开关：
   - `CH6 > 1500`：轮毂电机急停，升降机构停止。
   - `CH6 < 500`：轮毂电机使能，并清除急停状态。
7. `CH8` 控制升降机构：
   - `CH8 < 500`：上升。
   - `CH8 > 1500`：下降。
   - 其他范围：停止。
8. 升降机构状态有软件记忆，只有目标状态变化时才重新发送升降指令，避免持续刷指令。
9. 通过 USB CDC 虚拟串口周期上报左轮反馈转速、右轮反馈转速、当前移动速度、运行状态和升降高度。
10. 电脑端提供 `PC_Tools/telemetry_monitor.py` 实时监控程序，左侧显示关键数据，右侧显示左轮、右轮和总速度曲线。
11. 启动阶段会连续发送 8 次 `lift_up()`，用于上电后的升降机构初始动作。

## 硬件与串口分配

| 模块 | 外设 | 参数 | 代码位置 | 用途 |
| --- | --- | --- | --- | --- |
| SBUS 接收机 | UART5 | 100000 bps，偶校验，2 停止位，9B 字长配置 | `Core/Src/usart.c` | 接收 HT-10A 遥控器 SBUS 信号 |
| 轮毂电机 RS485 | USART2 | 115200 bps，8N1 | `Core/Src/usart.c` | 控制左右 UM 轮毂一体机 |
| 升降机构 RS485 | USART3 | 9600 bps，8N1 | `Core/Src/usart.c` | 控制升降机构 |
| USB 虚拟串口 | USB_OTG_HS 内部 FS PHY + USB_DEVICE CDC | 48 MHz USB 时钟，CDC ACM | `USB_DEVICE/`、`Middlewares/` | Type-C 连接电脑，发送实时遥测 |
| 调试串口 | USART1 | 115200 bps，8N1 | `Core/Src/usart.c` | 预留调试输出 |
| LCD | SPI1 | 主机发送 | `Core/Src/lcd.c` | 显示调试信息，当前主循环显示逻辑被注释 |
| ADC 按键 | ADC1 + DMA | PA5 / ADC1_INP19 | `Core/Src/adc.c` | 已初始化，当前主控制逻辑未使用 |
| 定时器 | TIM2 | 周期中断 | `Core/Src/tim.c` | 设置 `motor_read_flag`，当前主循环未使用该标志 |

RS485 方向控制引脚：

- 轮毂电机 RS485：`RS485_DE`，PD4。
- 升降机构 RS485：`RS485_DE2`，PB14。

## 遥控器通道映射

HT-10A 默认通道资料中，左摇杆上下为 `CH3`，左摇杆左右为 `CH4`，`SWB` 为 `CH6`，`SWD` 为 `CH8`。当前代码使用如下映射：

| 遥控器输入 | 代码通道 | 当前用途 |
| --- | --- | --- |
| 左摇杆上下 | `ch[2]` / CH3 | 前进、后退速度 |
| 左摇杆左右 | `ch[3]` / CH4 | 左右转向 |
| SWB | `ch[5]` / CH6 | 急停、使能 |
| SWD | `ch[7]` / CH8 | 升降机构上升、下降、停止 |

轮毂电机速度计算：

```c
desired_speed = (ch[2] - 992) / 32;
desired_steer = (ch[3] - 988) / 32;

desired_left_rpm  = desired_speed + desired_steer / 4;
desired_right_rpm = desired_speed - desired_steer / 4;

speed_set(desired_left_rpm, -desired_right_rpm);
```

右轮速度取负号，是为了适配当前左右电机安装方向。

## 重要函数说明

### `main.c`

| 函数 | 作用 |
| --- | --- |
| `main()` | 完成 HAL、系统时钟、GPIO、DMA、USART、SPI、ADC、TIM、LCD、SBUS 初始化，并在主循环中根据遥控器通道控制轮毂电机和升降机构。 |
| `SystemClock_Config()` | 配置 STM32H723 系统时钟，当前主频配置为 480 MHz。 |
| `telemetry_process()` | 每 200 ms 通过 USB CDC 发送缓存的 JSON 遥测帧，并在后台推进非阻塞 Modbus 查询状态机。 |
| `telemetry_process_poll_only()` | 主循环开头只轮询已有遥测回包，不新发查询，避免影响随后到来的遥控控制命令。 |
| `joystick_deadzone()` | 摇杆死区处理函数，当前未在主循环中使用。 |
| `accel_limit()` | 速度变化限幅函数，当前未在主循环中使用。 |

### `SBUS.c`

| 函数 | 作用 |
| --- | --- |
| `SBUS_Init()` | 开启 UART5 单字节中断接收。 |
| `SBUS_ParseChannels()` | 将 25 字节 SBUS 帧解析为 16 个 11 位通道值。 |
| `HAL_UART_RxCpltCallback()` | 串口接收完成回调。UART5 分支负责 SBUS 帧接收，USART2 分支负责解析轮毂电机返回的速度帧。 |
| `HAL_UART_ErrorCallback()` | UART5 出错时清除错误标志并重启 SBUS 接收。 |
| `SBUS_TimeoutCheck()` | 超过 30 ms 未收到 SBUS 字节时，将 `sbus_failsafe` 置为 1。 |
| `SBUS_Receive()` | 旧版/备用 SBUS 接收函数，当前主流程没有直接调用。 |

### `move_mode_control.c`

| 函数 | 作用 |
| --- | --- |
| `Modbus_CRC16()` | 计算 Modbus RTU CRC16 校验值。 |
| `motor_start_init()` | 向轮毂电机写入速度模式、加速度、减速度并使能。当前在 `main()` 中被注释，说明当前依赖电机参数已经提前设置好。 |
| `change_station()` | 修改电机站号，将站号从 `1` 改为 `2`，用于配置右轮电机站号。正常运行时不要反复调用。 |
| `speed_set()` | 限制左右轮目标转速到 `MAX_RPM` 范围内，并分别向站号 `1`、`2` 写入速度指令。 |
| `motor_read_speed()` | 读取指定站号电机的反馈转速，读取地址为 `0x5000`。 |
| `lift_read_height()` | 读取升降机构当前位置，读寄存器 `0x0002`，当前按 `1 = 1 mm` 显示。 |
| `motor_stop()` | 失能左右轮毂电机。 |
| `motor_enable()` | 使能左右轮毂电机。 |
| `motor_emergency_stop()` | 对左右轮写入急停指令，然后调用 `motor_stop()` 失能电机。 |
| `motor_clear_emergency_stop()` | 先使能电机，再清除左右轮急停标志。 |
| `motor_scan_address()` | 从站号 1 到 10 扫描可响应的电机地址。当前主流程未使用。 |
| `lift_up()` | 通过升降机构 RS485 发送上升指令。 |
| `lift_down()` | 通过升降机构 RS485 发送下降指令。 |
| `lift_stop()` | 通过升降机构 RS485 发送停止指令。 |
| `lift_reset()` | 通过升降机构 RS485 发送复位指令。当前主流程未使用。 |

### `usart.c`

| 函数 | 作用 |
| --- | --- |
| `RS485_SendPacket()` | 使用 USART2 发送轮毂电机 Modbus 指令，发送前拉高 PD4，发送完成后拉低 PD4 回到接收模式。 |
| `RS485_SendPacket2()` | 使用 USART3 发送升降机构 Modbus 指令，发送前拉高 PB14，发送完成后拉低 PB14 回到接收模式。 |
| `RS485_SendPacketTimeout()` | USART2 短超时发送函数，供后台遥测查询使用，避免发送异常时长时间卡住。 |
| `RS485_SendPacket2Timeout()` | USART3 短超时发送函数，供后台遥测查询使用，避免发送异常时长时间卡住。 |
| `RS485_ReceivePacket()` | USART2 阻塞式接收一帧数据。 |
| `RS485_ReceivePacket2()` | USART3 阻塞式接收一帧数据。 |
| `RS485_Receive_All()` | USART2 通用接收函数，按超时时间尽可能接收多字节。当前主流程未使用。 |

### `tim.c`

| 函数 | 作用 |
| --- | --- |
| `HAL_TIM_PeriodElapsedCallback()` | TIM2 周期中断中将 `motor_read_flag` 置 1。当前主循环还没有使用这个标志去周期读取转速。 |

## 重要变量说明

| 变量/宏 | 位置 | 意义 |
| --- | --- | --- |
| `SBUS_FRAME_LEN` | `Core/Inc/SBUS.h` | SBUS 单帧长度，当前为 25 字节。 |
| `SBUS_NUM_CHANNELS` | `Core/Inc/SBUS.h` | SBUS 解析通道数，当前为 16。 |
| `sbus_buf` | `Core/Src/SBUS.c` | UART5 中断接收到的 SBUS 原始帧缓存。 |
| `sbus_frame_ok` | `Core/Src/SBUS.c` | SBUS 完整帧接收完成标志。主循环看到该标志后复制缓存并解析。 |
| `sbus_failsafe` | `Core/Src/SBUS.c` | SBUS 失控/超时标志。 |
| `sbus_frame_lost` | `Core/Src/SBUS.c` | SBUS 帧丢失标志。 |
| `ch[16]` | `Core/Src/main.c` | 主循环中的本地 SBUS 通道数组。 |
| `MAX_RPM` | `Core/Inc/move_mode_control.h` | 轮毂电机目标转速限幅，当前为 `32 rpm`。 |
| `current_lift_state` | `Core/Src/main.c` | 当前升降机构软件状态，用于避免重复发送同一状态指令。 |
| `LiftState` | `Core/Inc/move_mode_control.h` | 升降状态枚举：`LIFT_STOP`、`LIFT_UP`、`LIFT_DOWN`。 |
| `uart2_rx_buf[7]` | `Core/Src/main.c` | 轮毂电机速度反馈帧接收缓存。 |
| `uart2_rx_done` | `Core/Src/main.c` | 轮毂电机速度反馈帧接收并校验成功标志。 |
| `motor_real_speed` | `Core/Src/main.c` | 最近一次解析到的轮毂电机反馈速度。 |
| `left` / `right` | `Core/Src/main.c` | 最近一次 USB 遥测读取到的左右轮反馈转速。 |
| `current_speed_rpm` | `Core/Src/main.c` | 当前移动速度，按左右轮安装方向修正后计算为 `(left - right) / 2`，单位 rpm。 |
| `lift_height_mm` | `Core/Src/main.c` | 升降机构高度，单位 mm；读取失败时为 `-1`。 |
| `telemetry_query_state` | `Core/Src/main.c` | 后台遥测查询状态机，按左轮、右轮、升降高度顺序非阻塞推进。 |
| `telemetry_failsafe` | `Core/Src/main.c` | USB 遥测使用的遥控失联状态标志。 |
| `emergency_stop` | `Core/Src/main.c` | 软件急停状态标志。 |
| `en_flag` | `Core/Src/main.c` | 电机使能状态标志。 |
| `motor_read_flag` | `Core/Src/main.c` | TIM2 置位的周期读速度标志，当前未接入主循环读取逻辑。 |
| `adc_val[1]` | `Core/Src/main.c` | ADC DMA 采样缓存，当前主控制逻辑未使用。 |

## 当前使用的 Modbus 指令含义

### 轮毂电机 USART2

| 功能 | 站号 | 寄存器 | 说明 |
| --- | --- | --- | --- |
| 速度模式 | `1` | `0x2102 = 1` | 设置为内部速度模式。 |
| 使能 | `1` / `2` | `0x2100 = 1` | 电机内部使能。 |
| 失能 | `1` / `2` | `0x2100 = 0` | 电机停止/失能。 |
| 速度指令 | `1` / `2` | `0x2318 = rpm` | 写入目标速度，单位 rpm。 |
| 加速时间 | `1` | `0x2320 = 0x01F4` | 初始化中设置加速时间。 |
| 减速时间 | `1` | `0x2321 = 0x01F4` | 初始化中设置减速时间。 |
| 急停 | `1` / `2` | `0x2322 = 1` | 进入急停状态。 |
| 清急停 | `1` / `2` | `0x2322 = 0` | 清除急停状态。 |
| 读反馈速度 | `1` / `2` | `0x5000` | 读取电机反馈转速。 |
| 修改站号 | `1` | `0x4503 = 2` | 配置右轮站号时使用。 |

### 升降机构 USART3

当前代码对升降机构使用站号 `1`、功能码 `0x06`、寄存器 `0x0001`：

| 函数 | 写入值 | 作用 |
| --- | --- | --- |
| `lift_stop()` | `0x0001` | 停止 |
| `lift_up()` | `0x0002` | 上升 |
| `lift_down()` | `0x0004` | 下降 |
| `lift_reset()` | `0x0008` | 复位 |

读取升降机构高度使用功能码 `0x03` 读寄存器 `0x0002`，命令为：

```text
01 03 00 02 00 01 25 CA
```

返回数据按 16 位有符号数解析，当前按 `1 = 1 mm` 作为高度显示。

## USB 遥测协议

单片机通过 USB CDC 虚拟串口每约 200 ms 发送一行 UTF-8 JSON，以 `\r\n` 结尾。示例：

```json
{"left_rpm":12,"right_rpm":-11,"speed_rpm":11,"state":"RUN","height_mm":245}
```

字段含义：

| 字段 | 含义 |
| --- | --- |
| `left_rpm` | 左轮反馈转速，单位 rpm。 |
| `right_rpm` | 右轮反馈转速，单位 rpm；因右轮安装方向可能与左轮相反，符号按电机实际反馈保留。 |
| `speed_rpm` | 固件保留的等效平均轮速，单位 rpm；当前上位机不再用该字段计算显示速度。 |
| `state` | 运行状态：`RUN`、`DISABLED`、`ESTOP`、`FAILSAFE`。 |
| `height_mm` | 升降机构高度，单位 mm；读取失败时为 `-1`。 |

## 电脑端监控程序

监控程序位于：

```text
PC_Tools/telemetry_monitor.py
```

首次运行前安装依赖：

```powershell
pip install -r PC_Tools\requirements.txt
```

运行：

```powershell
python PC_Tools\telemetry_monitor.py
```

打开后选择 STM32 枚举出的 COM 口并点击“连接”。界面左侧显示左轮转速、右轮转速、当前移动速度、运行状态和升降机构高度；右侧实时绘制左轮、右轮和当前移动速度曲线。上位机显示时会对左右轮转速取绝对值，左轮/右轮单位保持 rpm；当前移动速度由左右轮绝对转速和轮半径 `0.075 m` 换算得到，单位为 m/s。

如果不想从 VSCode 或命令行启动，可以直接双击打包后的程序：

```text
PC_Tools/dist/telemetry_monitor.exe
```

当前 exe 使用 PyInstaller 单文件模式生成，首次启动会先解包运行环境，因此会比直接运行 Python 脚本慢几秒。需要重新打包时，在工程根目录运行：

```powershell
python -m pip install pyinstaller
$icon = (Resolve-Path PC_Tools\assets\app_icon.ico).Path
python -m PyInstaller --noconfirm --onefile --windowed --name telemetry_monitor --icon "$icon" --add-data "$icon;assets" --distpath PC_Tools\dist --workpath PC_Tools\build --specpath PC_Tools PC_Tools\telemetry_monitor.py
```

需要记录数据时，点击“连接”旁边的“记录”按钮。程序会在 `PC_Tools/data/` 下创建一次采集会话目录，目录名格式为 `YYYY-MM-DD_HH-MM-SS`，例如：

```text
PC_Tools/data/2026-05-31_18-30-01/
  raw.jsonl
  telemetry.csv
  session_info.json
```

`raw.jsonl` 保存每帧原始 JSON 和上位机归一化后的字段，便于后续扩展左右轮力矩等新字段；`telemetry.csv` 适合直接用 Excel、Origin、MATLAB 或 Python pandas 分析。当前 CSV 字段为 `pc_time,time_s,left_rpm,right_rpm,speed_mps,state,height_mm,left_torque,right_torque`。如果单片机帧中缺少某个字段，或字段为 `null` / 空值 / 无法解析，JSONL 中记录为 `null`，CSV 中保留空单元格；如果单片机实际发送数值 `0`，上位机会按真实数据记录为 `0`。

## 当前运行流程

1. 上电后初始化 HAL、系统时钟、GPIO、DMA、串口、SPI、ADC、TIM2。
2. 初始化 LCD 并清屏。
3. ADC1 开启 DMA 采样。
4. TIM2 开启周期中断。
5. 调用 `SBUS_Init()` 开始 UART5 中断接收。
6. 启动阶段连续调用 8 次 `lift_up()`。
7. 主循环中调用 `SBUS_TimeoutCheck()` 检查遥控器是否超时。
8. 收到完整 SBUS 帧后，关中断复制 `sbus_buf`，再开中断解析本地帧。
9. 如果 `CH6 > 1500`，立即电机急停并停止升降。
10. 如果 `CH6 < 500`，执行电机使能与清急停。
11. 根据 `CH3`、`CH4` 计算左右轮目标转速，并调用 `speed_set()`。
12. 根据 `CH8` 控制升降机构上升、下降或停止。
13. 主循环开头先快速轮询已有遥测回包，不新发查询，确保随后遥控控制命令优先执行。
14. 主循环末尾推进后台遥测状态机，按左轮、右轮、升降高度顺序发起短超时查询。
15. USB CDC 每约 200 ms 发送一次 JSON 遥测帧；若某次查询超时或 CRC 错误，继续使用上一次缓存值。

## 注意事项

1. 当前 `motor_start_init()` 在 `main()` 中被注释。如果更换新电机，或电机参数没有保存为速度模式，需要先通过上位机或代码初始化电机参数。
2. `SBUS_TimeoutCheck()` 已经能把超时视为 failsafe，但 `main()` 中 failsafe 分支里的 `motor_emergency_stop()`、`lift_stop()` 目前被注释。因此现在信号丢失时不会主动急停，这是后续应该优先打开的安全逻辑。
3. `joystick_deadzone()` 和 `accel_limit()` 已经写好，但当前速度控制没有使用死区和加速度限幅。摇杆中位漂移或速度突变明显时，应把这两个函数接入主循环。
4. `motor_read_speed()` 已接入 USB 遥测，LCD 显示代码仍保持注释，避免和电脑端监控重复。
5. `change_station()` 会修改电机站号，不应在正常遥控运行时调用。
6. 源码中部分中文注释存在编码显示异常，代码逻辑本身不受影响；后续整理注释时应统一文件编码，避免 CubeMX 再生成后继续乱码。

## 建议的下一步改进

1. 打开 SBUS 失控保护，让信号丢失时强制电机急停、升降停止。
2. 接入摇杆死区，避免中位轻微抖动导致轮毂电机低速爬行。
3. 接入加速度限幅，让轮毂电机速度变化更平滑。
4. 将启动阶段连续 `lift_up()` 改为明确的回零/复位流程，避免上电即上升带来的机械风险。
5. 如需进一步降低遥测开销，可把当前非阻塞轮询升级为 UART ReceiveToIdle DMA；但 DMA 不是必要前提，现阶段主控不会等待遥测回包。

## 大版本改动记录

### v1.0 遥控运动控制基础版

- 完成 HT-10A 遥控器 SBUS 接收与通道解析。
- 通过 `CH3`、`CH4` 计算差速目标转速，并经 USART2/RS485 控制左右 UM 一体式轮毂电机。
- 通过 `CH6` 实现急停/使能控制，通过 `CH8` 实现升降机构上升、下降和停止。
- 保留 LCD、ADC、USART1 调试口等基础外设初始化。

### v1.1 USB CDC 配置版

- 在 CubeMX 中启用 `USB_OTG_HS` 的内部 FS PHY，配置为 Device Only。
- 启用 USB_DEVICE CDC 类，生成 `USB_DEVICE/` 和 `Middlewares/ST/STM32_USB_Device_Library/`。
- USB 时钟使用 `HSI48 = 48 MHz`，系统主频保持 `SYSCLK = 480 MHz`、`HCLK = 240 MHz`。
- Keil 工程加入 USB Device Core、CDC Class、PCD 和 LL USB 源文件。
- 关闭 CubeMX 的 `Delete previously generated files when not re-generated`，避免再次误删 `Drivers/`。

### v2.0 USB 实时监控版

- 固件新增 USB CDC JSON 遥测，每约 200 ms 上报左右轮反馈转速、当前移动速度、运行状态和升降高度。
- `motor_read_speed()` 改为阻塞读取并校验 Modbus RTU CRC，确保左右轮各自读数明确。
- 新增 `lift_read_height()`，按 `01 03 00 02 00 01 25 CA` 读取升降机构高度，按 `1 = 1 mm` 显示。
- 优化急停/使能分支，避免急停或使能指令在每帧 SBUS 中重复发送导致主循环长时间阻塞。
- 新增 Python Tkinter + Matplotlib 上位机 `PC_Tools/telemetry_monitor.py`，支持串口选择、实时数值展示和三条速度曲线。
- 本地 Keil 工程编译器配置调整为已安装的 Arm Compiler `6.22`，并完成 `0 Error(s)` 编译验证。

### v2.1 上位机界面优化版

- 修复 Matplotlib 图表中文显示为方块的问题，启动时优先加载 Windows 中文字体 `Microsoft YaHei`，并回退到 `SimHei` / `SimSun`。
- 优化上位机监控界面配色，将图表区域改为深色面板，统一网格、坐标轴、图例和曲线颜色。
- 左轮、右轮、总速度曲线颜色调整为更柔和的蓝色、黄色和绿色，并与左侧实时数据卡片保持一致。

### v2.2 上位机速度显示修正版

- 上位机不再直接显示固件发送的 `speed_rpm`，而是用 `left_rpm` 和 `right_rpm` 在电脑端重新计算当前移动速度。
- 左轮和右轮转速显示改为 `abs(left_rpm)`、`abs(right_rpm)`，单位仍为 rpm。
- 当前移动速度按 `((abs(left_rpm) + abs(right_rpm)) / 2) * 2π * 0.075 / 60` 换算为 m/s，并保留 3 位小数。
- 图表改为双 Y 轴：左轴显示左右轮 rpm，右轴显示当前移动速度 m/s。

### v2.3 遥测阻塞与命令 CRC 修正版

- 保持遥控轮毂和升降机构主控制逻辑不变，仅优化 USB 遥测读数流程。
- USB 遥测读取改为分时轮询：左轮、右轮、升降高度分 3 个周期读取，避免单次遥测最坏连续阻塞约 180 ms。
- `motor_start_init()` 中原有 CRC 错误的硬编码初始化帧已保留为注释，并标明错误原因；实际发送改为动态计算 Modbus CRC。
- `motor_scan_address()` 的 2 寄存器读取响应长度从 7 字节修正为 9 字节，并增加注释说明原因。

### v2.4 上位机数据记录版

- 上位机新增“记录”按钮，支持按需开始/停止采集日志。
- 记录目录从 `logs` 方案改为 `PC_Tools/data/YYYY-MM-DD_HH-MM-SS/`，每次记录单独建目录。
- 每次记录保存 `raw.jsonl`、`telemetry.csv` 和 `session_info.json` 三个文件，便于后续做转速、速度、力矩等数据分析。
- 缺失字段不再被默认写成 `0`：JSONL 使用 `null`，CSV 使用空单元格；单片机真实发送的 `0` 仍按 `0` 记录。
- `PC_Tools/data/` 已加入 `.gitignore`，采集数据留在本地，不进入代码仓库。

### v2.5 固件遥测非阻塞优化版

- 固件遥测读取从阻塞式 `motor_read_speed()` / `lift_read_height()` 改为后台状态机，不再等待完整回包后才返回主循环。
- 状态机按左轮、右轮、升降高度顺序推进；等待响应期间主循环继续执行遥控解析和运动控制。
- 左右轮读取超时设为 `10 ms`，升降高度读取超时设为 `15 ms`；超时或 CRC 错误时保留上一次缓存值。
- 遥控、电机和升降写命令优先：控制命令发出前会中止正在等待的后台遥测查询，控制命令结束后再允许继续查询。
- USB CDC JSON 协议保持不变，上位机仍接收 `left_rpm`、`right_rpm`、`speed_rpm`、`state`、`height_mm`。
