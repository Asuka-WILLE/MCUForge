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
```

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
9. 具备读取轮毂电机反馈转速的函数，LCD 显示代码也已经写好，但主循环中目前被注释。
10. 启动阶段会连续发送 8 次 `lift_up()`，用于上电后的升降机构初始动作。

## 硬件与串口分配

| 模块 | 外设 | 参数 | 代码位置 | 用途 |
| --- | --- | --- | --- | --- |
| SBUS 接收机 | UART5 | 100000 bps，偶校验，2 停止位，9B 字长配置 | `Core/Src/usart.c` | 接收 HT-10A 遥控器 SBUS 信号 |
| 轮毂电机 RS485 | USART2 | 115200 bps，8N1 | `Core/Src/usart.c` | 控制左右 UM 轮毂一体机 |
| 升降机构 RS485 | USART3 | 9600 bps，8N1 | `Core/Src/usart.c` | 控制升降机构 |
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

## 注意事项

1. 当前 `motor_start_init()` 在 `main()` 中被注释。如果更换新电机，或电机参数没有保存为速度模式，需要先通过上位机或代码初始化电机参数。
2. `SBUS_TimeoutCheck()` 已经能把超时视为 failsafe，但 `main()` 中 failsafe 分支里的 `motor_emergency_stop()`、`lift_stop()` 目前被注释。因此现在信号丢失时不会主动急停，这是后续应该优先打开的安全逻辑。
3. `joystick_deadzone()` 和 `accel_limit()` 已经写好，但当前速度控制没有使用死区和加速度限幅。摇杆中位漂移或速度突变明显时，应把这两个函数接入主循环。
4. `motor_read_speed()` 和 LCD 显示代码目前被注释，实际运行时不会周期显示左右轮反馈转速。
5. `change_station()` 会修改电机站号，不应在正常遥控运行时调用。
6. 源码中部分中文注释存在编码显示异常，代码逻辑本身不受影响；后续整理注释时应统一文件编码，避免 CubeMX 再生成后继续乱码。

## 建议的下一步改进

1. 打开 SBUS 失控保护，让信号丢失时强制电机急停、升降停止。
2. 接入摇杆死区，避免中位轻微抖动导致轮毂电机低速爬行。
3. 接入加速度限幅，让轮毂电机速度变化更平滑。
4. 恢复周期读取左右轮反馈速度，并在 LCD 或调试串口输出。
5. 将启动阶段连续 `lift_up()` 改为明确的回零/复位流程，避免上电即上升带来的机械风险。
