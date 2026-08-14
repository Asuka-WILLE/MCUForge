# UM10550 轮毂电机与升降机构遥控控制程序

> **MCUForge 比赛分支说明（2026-08-14）**：`feature/mcuforge-demo-infra` 面向“开发板 + TFT + 电脑”演示，不需要车、遥控器或电机。`MCUFORGE_DEMO_MODE=1` 时主循环只运行隔离的 USB 虚拟控制模块，不进入下文保留的 SBUS、RS485 和实际电机流程。

## MCUForge 开发板 Demo

本分支新增的真实链路为：

```text
电脑监控与虚拟手柄
  -> USB CDC 14 字节控制帧（20 ms）
  -> STM32 协议校验与虚拟左右混控
  -> TFT 显示 PC 输入和虚拟输出
  -> USB CDC JSON 遥测回传电脑并保存测试证据
```

关键入口：

- 固件：`Core/Src/mcuforge_demo.c`、`Core/Inc/mcuforge_demo.h`
- GUI：`python PC_Tools\telemetry_monitor.py`
- 固定测试：`python PC_Tools\mcuforge_test_runner.py --list`
- 多 Agent 基础设施：`agent_infra/README.md`
- 串口协议：`docs/serial-control-protocol.md`

当前开发板仍运行旧固件；新 Demo 源码已经通过 Keil 全量构建，但尚未获得烧录许可。基础版本故意不实现 150 ms 失联清零、三帧中位恢复和急停锁定，用固定测试留下一个可由 AgentTeams 编码、复测和审计的真实任务。

下文继续保留原生产车控工程说明，便于理解被 Demo 模式隔离的既有能力。

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
- MC-02 官方 BMI088 例程：`C:\Users\hz_wu\Desktop\轮式人形资料\MC02开发板\dm-mc02-master\例程\CtrBoard-H7_IMU`
- 社区 BSP 参考：<https://github.com/yssickjgd/damiao_mc02_bsp>

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
11. 上电后只向升降机构发送一次停止命令，不自动升降。
12. 已接入 MC-02 板载 BMI088 的 SPI2 底层驱动和 `IMU_Init()` / `IMU_Poll()` / `IMU_CopyLatest()` 接口；初始化与采样调用暂时注释，不改变当前控制和 USB 遥测行为。

## 硬件与串口分配

| 模块 | 外设 | 参数 | 代码位置 | 用途 |
| --- | --- | --- | --- | --- |
| SBUS 接收机 | UART5 | 100000 bps，偶校验，2 停止位，9B 字长配置 | `Core/Src/usart.c` | 接收 HT-10A 遥控器 SBUS 信号 |
| 轮毂电机 RS485 | USART2 | 115200 bps，8N1 | `Core/Src/usart.c` | 控制左右 UM 轮毂一体机 |
| 升降机构 RS485 | USART3 | 9600 bps，8N1 | `Core/Src/usart.c` | 控制升降机构 |
| USB 虚拟串口 | USB_OTG_HS 内部 FS PHY + USB_DEVICE CDC | 48 MHz USB 时钟，CDC ACM | `USB_DEVICE/`、`Middlewares/` | Type-C 连接电脑，发送实时遥测 |
| 调试串口 | USART1 | 115200 bps，8N1 | `Core/Src/usart.c` | 预留调试输出 |
| LCD | SPI1 | 主机发送 | `Core/Src/lcd.c` | 显示 SBUS、使能、失联和目标状态 |
| 板载 BMI088 | SPI2 | Mode 3，7.5 Mbit/s；PC0/PC3 片选，PE10/PE12 数据就绪 | `Core/Src/imu.c` | 原始加速度、角速度和温度接口；当前未启用采样 |
| ADC 按键 | ADC1 + DMA | PA5 / ADC1_INP19 | `Core/Src/adc.c` | 已初始化，当前主控制逻辑未使用 |

BMI088 使用 `PB13` 作为 SPI2 时钟，因此 UART5 已改为只接收 SBUS（`PD2/UART5_RX`）；工程没有 UART5 发送调用。

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

motor_target_linear = desired_speed;
motor_target_steer = desired_steer;

motor_trajectory_update(motor_target_linear,
                        motor_target_steer,
                        &desired_left_rpm,
                        &desired_right_rpm);
motor_speed_control_update(desired_left_rpm, -desired_right_rpm);
```

右轮速度取负号，是为了适配当前左右电机安装方向。

## 重要函数说明

### `main.c`

| 函数 | 作用 |
| --- | --- |
| `main()` | 完成 HAL、系统时钟、GPIO、DMA、USART、SPI、ADC、LCD、SBUS 初始化，并在主循环中根据遥控器通道控制轮毂电机和升降机构。 |
| `SystemClock_Config()` | 配置 STM32H723 系统时钟，当前主频配置为 480 MHz。 |
| `telemetry_process()` | 每 50 ms 通过 USB CDC 发送缓存的 JSON 遥测帧，并在后台推进非阻塞 Modbus 查询状态机。 |
| `telemetry_process_poll_only()` | 主循环开头只轮询已有遥测回包，不新发查询，避免影响随后到来的遥控控制命令。 |
| `motor_write_speed_with_echo()` | 写入单台驱动器 `0x2318` 后接收并逐字节校验 Modbus `0x06` 应答。 |
| `motor_speed_set_confirmed()` | 保持右轮先发，在后台查询、右轮应答和左轮写入之间保留 2 ms 静默间隔，并统计两轮写入成功/失败。 |
| `straight_sync_apply()` | 直线启动时只使用左右轮成对的新鲜 `0.1 rpm` 反馈并削减快轮；停车尾段允许在两侧缓存均不老于 80 ms 时随任一新反馈更新，只把仍在运动的轮子继续削向零。 |
| `joystick_deadzone()` | 对 CH3/CH4 做中位死区处理。 |

### `imu.c`

| 函数 | 作用 |
| --- | --- |
| `IMU_Init()` | 检查 BMI088 加速度计/陀螺仪芯片 ID，软复位并写入经回读校验的 ±3 g、±2000 °/s 配置；失败直接返回错误，不死循环。 |
| `IMU_Poll()` | 最快每 10 ms 读取一次原始三轴加速度、三轴角速度和温度，换算为 `m/s²`、`rad/s`、`°C`。 |
| `IMU_CopyLatest()` | 向后续 USB CDC 或姿态算法复制最近一组有效样本及时间戳、序号。 |

`main.c` 中的 `IMU_Init()` 与 `IMU_Poll()` 调用目前均被注释。当前样本保持 BMI088 芯片原生坐标轴，尚未加入板体坐标变换、静态零偏标定、姿态融合或 USB 字段。温控加热同样未实现、未使能。

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
| `motor_stop()` | 失能左右轮毂电机。 |
| `motor_enable()` | 先将两台驱动器目标清零，并统一写入速度模式、加速时间和减速时间，再使能左右轮毂电机。 |
| `motor_emergency_stop()` | 对左右轮写入急停指令，然后调用 `motor_stop()` 失能电机。 |
| `motor_clear_emergency_stop()` | 先清除左右轮急停标志，再执行统一配置和使能流程。 |
| `lift_up()` | 通过升降机构 RS485 发送上升指令。 |
| `lift_down()` | 通过升降机构 RS485 发送下降指令。 |
| `lift_stop()` | 通过升降机构 RS485 发送停止指令。 |

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
| `left` / `right` | `Core/Src/main.c` | 由 `0x500E` 原始值四舍五入得到的整数 rpm 兼容值，供旧显示和停稳状态判断使用。 |
| `left_speed_x10` / `right_speed_x10` | `Core/Src/main.c` | `0x500E` 返回的原始有符号轮速，单位 `0.1 rpm`；启动同步直接使用该精度。 |
| `current_speed_rpm` | `Core/Src/main.c` | 当前移动速度，按左右轮安装方向修正后计算为 `(left - right) / 2`，单位 rpm。 |
| `lift_height_mm` | `Core/Src/main.c` | 升降机构高度，单位 mm；读取失败时为 `-1`。 |
| `telemetry_query_state` | `Core/Src/main.c` | 后台遥测查询状态机；以 20 ms 调度周期优先查询左右轮，静止时插入扩展诊断，升降高度每 500 ms 查询一次，控制写入始终优先。 |
| `telemetry_failsafe` | `Core/Src/main.c` | USB 遥测使用的遥控失联状态标志。 |
| `emergency_stop` | `Core/Src/main.c` | 软件急停状态标志。 |
| `en_flag` | `Core/Src/main.c` | 电机使能状态标志。 |
| `adc_val[1]` | `Core/Src/main.c` | ADC DMA 采样缓存，当前主控制逻辑未使用。 |

## 当前使用的 Modbus 指令含义

### 轮毂电机 USART2

| 功能 | 站号 | 寄存器 | 说明 |
| --- | --- | --- | --- |
| 速度模式 | `1` / `2` | `0x2102 = 1` | 两台驱动器统一设置为内部速度模式。 |
| 使能 | `1` / `2` | `0x2100 = 1` | 电机内部使能。 |
| 失能 | `1` / `2` | `0x2100 = 0` | 电机停止/失能。 |
| 速度指令 | `1` / `2` | `0x2318 = rpm` | 写入目标速度，单位 rpm。 |
| 加速时间 | `1` / `2` | `0x2320 = 0x0064` | 两台驱动器统一使用较短内部加速时间，主要平滑由 STM32 S 曲线负责。 |
| 减速时间 | `1` / `2` | `0x2321 = 0x0064` | 两台驱动器统一使用较短内部减速时间，减少停车拖尾和转向。 |
| 急停 | `1` / `2` | `0x2322 = 1` | 进入急停状态。 |
| 清急停 | `1` / `2` | `0x2322 = 0` | 清除急停状态。 |
| 高精度反馈速度 | `1` / `2` | `0x500E` | 当前后台遥测读取，原始单位 `0.1 rpm`。 |

### 升降机构 USART3

当前代码对升降机构使用站号 `1`、功能码 `0x06`、寄存器 `0x0001`：

| 函数 | 写入值 | 作用 |
| --- | --- | --- |
| `lift_stop()` | `0x0001` | 停止 |
| `lift_up()` | `0x0002` | 上升 |
| `lift_down()` | `0x0004` | 下降 |

读取升降机构高度使用功能码 `0x03` 读寄存器 `0x0002`，命令为：

```text
01 03 00 02 00 01 25 CA
```

返回数据按 16 位有符号数解析，当前按 `1 = 1 mm` 作为高度显示。

## USB 遥测协议

单片机通过 USB CDC 虚拟串口每约 50 ms 发送一行 UTF-8 JSON，以 `\r\n` 结尾。示例：

当前 USB JSON 保持生产版协议不变，尚不发送 IMU 数据。后续启用时建议先调用 `IMU_CopyLatest()`，再增加 `imu_ax/ay/az`、`imu_gx/gy/gz`、`imu_temp_c`、`imu_seq` 和 `imu_age_ms` 字段，并同步修改 `PC_Tools/telemetry_monitor.py`。

```json
{"left_rpm":12,"right_rpm":-11,"left_rpm_x10":120,"right_rpm_x10":-111,"left_cmd":12,"right_cmd":-12,"sync_trim":0,"rc_ready":1,"rc_ch6":192,"sbus_failsafe":0,"state":"RUN","height_mm":245}
```

字段含义：

| 字段 | 含义 |
| --- | --- |
| `left_rpm` | 左轮反馈转速，单位 rpm。 |
| `right_rpm` | 右轮反馈转速，单位 rpm；因右轮安装方向可能与左轮相反，符号按电机实际反馈保留。 |
| `left_rpm_x10` / `right_rpm_x10` | 驱动器 `0x500E` 原始反馈，单位 `0.1 rpm`；PC 工具优先用这两个字段还原一位小数轮速。 |
| `speed_rpm` | 固件保留的等效平均轮速，单位 rpm；当前上位机不再用该字段计算显示速度。 |
| `left_cmd` / `right_cmd` | 最近一次发送给左右驱动器的有符号速度命令。 |
| `traj_speed_x100` / `traj_accel_x100` | STM32 轨迹速度和加速度，放大 100 倍传输。 |
| `sync_trim` / `sync_error_x100` | 左右轮同步削减量和滤波误差；正负号表示被削减的物理侧，不代表固定左右偏置。 |
| `sync_pair_sequence` / `sync_pair_skew_ms` | 严格成对同步样本序号及两轮采样时差；启动阶段只使用不超过 80 ms 的成对样本。 |
| `rc_ready` / `rc_ch3` / `rc_ch4` / `rc_ch6` | 遥控可信状态和关键通道值。 |
| `rc_age_ms` / `rc_link_age_ms` / `rc_frame_lost_count` | 最近被控制器接受的 SBUS 帧年龄、最近合法链路帧年龄及孤立丢帧累计值；快速摇杆确认不会再被误判为链路失联。 |
| `rc_stop_count` / `rc_recovery_count` / `rc_stop_reason` | 遥控真正失效、恢复次数及最近停机原因。 |
| `sbus_failsafe` | SBUS 失联/超时标志。 |
| `state` | 运行状态：`RUN`、`DISABLED`、`ESTOP`、`FAILSAFE`。 |
| `height_mm` | 升降机构高度，单位 mm；读取失败时为 `-1`。 |
| `speed_pair_sequence` / `left_speed_age_ms` / `right_speed_age_ms` | 轮速反馈更新序号及左右缓存年龄，用于识别陈旧读数。 |
| `motor_write_sequence` / `*_write_echo_ok` / `*_write_fail_count` | 双轮速度写入序号、最近应答状态及累计最终失败次数。 |

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

`raw.jsonl` 保存每帧原始 JSON 和上位机归一化后的字段；`telemetry.csv` 同时记录左右轮有符号反馈/绝对值、轮速差、驱动命令、轨迹、同步修正、遥控通道、运行状态、升降高度和驱动器诊断。如果单片机帧中缺少某个字段，或字段为 `null` / 空值 / 无法解析，JSONL 中记录为 `null`，CSV 中保留空单元格；如果单片机实际发送数值 `0`，上位机会按真实数据记录为 `0`。

### 无界面被动记录

下面的命令会被动记录 30 秒遥测并保存到 `PC_Tools/data/`：

```powershell
python PC_Tools\telemetry_monitor.py --headless --port COM3 --duration 30
```

PC 工具只读取 USB CDC 遥测，不再向单片机发送运动或停止命令；所有实车运动均由遥控器控制。

## 当前运行流程

1. 上电后初始化 HAL、系统时钟、GPIO、DMA、串口、SPI、ADC 和 USB。
2. 调用 `SBUS_Init()` 开始 UART5 中断接收，随后初始化 LCD。
3. ADC1 开启 DMA 采样，并向升降机构发送一次停止命令。
4. 主循环中调用 `SBUS_TimeoutCheck()` 检查遥控器是否超时。
5. 收到完整 SBUS 帧后，关中断复制 `sbus_buf`，再开中断解析本地帧。
6. 如果 `CH6 > 1500`，立即电机急停并停止升降。
7. 如果 `CH6 < 500` 且摇杆回中，执行电机使能与清急停。
8. 根据 `CH3`、`CH4` 得到底盘线速度和转向目标，先经公共 20 ms S 曲线，再解算左右轮；摇杆归中立即锁存零目标，轨迹只允许继续向零减速。
9. 根据 `CH8` 控制升降机构上升、下降或停止。
10. 主循环开头先快速轮询已有遥测回包，不新发查询，确保随后遥控控制命令优先执行。
11. 主循环末尾推进后台遥测状态机：运动时优先读取左右轮 `0x500E`；扩展驱动器诊断只在两轮命令和反馈均为零时进行；升降高度每 `500 ms` 查询一次。
12. USB CDC 每约 50 ms 发送一次 JSON 遥测帧；若某次查询超时或 CRC 错误，继续使用上一次缓存值。

## 注意事项

1. 每次软件使能都会对站号 1、2 写入统一的零速目标、速度模式和加减速参数，不再依赖驱动器历史保存值。
2. SBUS 单次 `frame_lost` 只作为诊断计数；接收机 `failsafe` 或连续 60 ms 没有可信帧才进入安全停车，持续 150 ms 后执行驱动器急停。
3. `CH3`、`CH4` 已使用中位死区；线速度和转向量均经过限加速度、限减速度和限 jerk 的公共 S 曲线。
4. USB CDC 接收端不再解析任何运动命令，电脑端工具也是纯被动遥测记录器。
5. MC-02 的 IMU 加热回路直接接入 24 V；当前工程没有配置或开启加热 PWM。完成 BMI088 原始数据实机验证和温控安全检查前，不要启用加热。
6. 当前仅初始化 SPI2 外设并保持 BMI088 两个片选为高；`IMU_Init()` 与 `IMU_Poll()` 仍被注释，因此不会增加主循环阻塞或改变电机控制时序。
5. 源码中部分中文注释存在编码显示异常，代码逻辑本身不受影响；后续整理注释时应统一文件编码，避免 CubeMX 再生成后继续乱码。

## 建议的下一步改进

1. 增加 `0x5015/0x5016` 累计位置诊断，用真实轮程而不是轮速积分评估可见偏航。
2. 基于现有内部目标、转矩、母线电压和故障码遥测，建立停车残速的判定阈值和自动标记。
3. 若现场仍能观察到转向松手拖尾，单独调整角速度停车 jerk；不要改动已通过验证的直线停车参数。
4. 为升降机构增加明确的回零/复位流程；当前上电只发送停止命令。
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
- 状态机按左轮、右轮、升降高度轮换查询；每次只读一个设备，等待响应期间主循环继续执行遥控解析和运动控制。
- 左右轮读取超时设为 `50 ms`，升降高度读取超时设为 `15 ms`；超时或 CRC 错误时保留上一次缓存值。
- 遥控、电机和升降写命令优先：控制命令发出前会中止正在等待的后台遥测查询，控制命令结束后再允许继续查询。
- 修正正常运行时速度命令每帧重复发送导致遥测长期被延后的问题；速度目标大幅变化或跨零点时立即发送，小幅抖动/目标不变时按 `100 ms` 周期刷新。普通保活刷新不会打断正在等待的遥测回包，保证 RUN 状态下仍能持续读取左右轮转速。
- USB CDC JSON 协议保持不变，上位机仍接收 `left_rpm`、`right_rpm`、`speed_rpm`、`state`、`height_mm`。

### v2.6 左右轮启动时序调整版

- `speed_set()` 中左右轮速度命令下发顺序调整为先右轮、后左轮，用于抵消原先左轮先收到命令导致的启动偏快现象。
- 原左轮命令后的 `50 ms` 等待被改为短帧间隔；右轮命令后等待 `2 ms`，左轮命令后等待 `5 ms`，在保留 RS485/Modbus 帧间隔的同时减少左右轮启动时间差。

### v2.7 LCD 遥控调试显示版

- 新增达妙官方 1.69 inch LCD 遥控调试显示页，实时显示 `CH3`、`CH4`、`CH6`、`CH8`、速度/转向映射、左右轮目标、升降状态、SBUS 丢帧/失控、使能和急停状态。
- 参考达妙 `CtrBoard-H7_LCD` 官方例程，恢复 LCD 横屏尺寸为 `280x240`，并将 SPI1 时钟极性恢复为 `SPI_POLARITY_HIGH`。
- 增加 LCD 开机边框和彩条自检，便于快速确认屏幕、背光和 SPI 通信是否正常。
- 已完成 Keil MDK 命令行编译验证：`0 Error(s), 0 Warning(s)`；硬件测试确认 LCD 显示正常。

### v2.8 万向轮平滑启停与实机调试版

- STM32 对底盘线速度和转向量分别执行固定周期 S 曲线，限制加速度、减速度和 jerk，再统一解算左右轮命令。
- 停车阶段由外层轨迹平滑减速；两台驱动器的内部加减速时间统一为 `0x0064`，消除一侧长时间拖尾造成的停车转向。
- 新增万向轮归正状态机：停车确认后进入 `CRAWL`，以 8 RPM 完成脚轮换向；两轮实际运动至少 500 ms 且轮速差连续 200 ms 不超过 1 RPM 后，才释放正常目标。
- 归正超时会进入 `FAILED` 并立即锁零，避免某一轮卡住时驱动器积分累积后突然猛冲。
- 前进零速启动采用两段静摩擦补偿：前 250 ms 每侧差动补偿 2 RPM，随后降为 1 RPM，直到两轮轮速匹配。
- 直线运行使用最大 1 RPM 的慢速交叉同步修正；转向、归正失败、停车和失联状态下自动清零。
- 两台轮毂驱动器在使能前统一写入零速目标、速度模式及相同加减速参数，不再依赖驱动器历史保存值。
- USB CDC 新增受限实机测试协议：`MOVE <linear> <steer> <duration_ms>` 和 `STOP`。仅在遥控可信、已使能、摇杆回中、双轮静止时接受，线速度限制为 ±20 RPM、转向量限制为 ±32、单次最长 10 秒。
- 任意遥控摇杆动作、急停、SBUS 失联或 watchdog 到期都会取消 USB 控制；超限指令直接拒绝。
- 遥测周期改为 50 ms，并增加轨迹、归正状态、USB 测试、同步修正和 RC 诊断字段；`PC_Tools/telemetry_monitor.py` 支持 `--headless` 自动记录和受限实机测试。

### v2.9 中位停车锁存修复版

- 正常遥控和 USB 测试指令直接进入公共 20 ms S 曲线，停用会延迟释放目标的 `CASTER_ALIGN_BRAKE/CRAWL` 路径。
- 摇杆线速度与转向同时归零后立即锁存停车请求；轨迹只能继续向零减速，任何新的非零指令可在下一个控制周期接管。
- 公共轨迹进入 2 RPM 低速尾段后统一锁零，并在随后 300 ms 内每 20 ms 重发双轮零速，降低丢帧或两轮静摩擦差异造成的单轮残速。
- 正常停车不复位整条公共轨迹、不切换电机使能，也不等待旧加速度衰减后才接受新操作。
- 实机调试确认：驱动器内部参数统一后，原 8 RPM 转向测试中的启动延迟和峰值差明显下降；内部减速时间缩短后，命令归零到双轮接近零的时间由约 2.90 秒缩短到约 0.25～0.35 秒。

### v3.0 RS485 写入确认与自适应起步版

- 修正 SBUS 单帧 `frame_lost` 被误判为完全失联的问题；进一步分离“合法链路帧时间”和“控制目标接受时间”，快速摇杆两帧确认期间不再触发假超时，真正 failsafe 和持续链路超时仍立即进入安全停车。
- 每次写入两台驱动器 `0x2318` 后接收、校验各自 `0x06` 应答；后台查询到右轮写、右轮应答到左轮写之间均保留 2 ms 静默间隔，消除了随机单轮保留旧目标。
- 左右轮反馈改为 30 ms 交替读取，任意一轮获得新反馈都会唤醒同步器；反馈超过 150 ms 不参与修正。
- 取消固定左右偏置。若一轮先克服静摩擦，只削减先动轮并动态限制到约 8 RPM；慢轮保留完整动力，两轮均运动后自动退回最大 2 RPM 的运行修正。
- USB 受限直线测试扩展到 ±32 RPM；自动序列必须检测到两轮连续约 300 ms 为 0 RPM 才允许下一次零速启动，并支持提前发送 `STOP` 验证归中锁存。
- 当前实测：±25 RPM、±32 RPM 各完成正反 5 次、每段运行 5 秒；停车尾段最大约 267 ms，速度写应答最终失败为 0。最大转向量松手后的残余旋转为 171～255 ms；四次提前归中测试均未出现零命令后再次运动命令。
- 快速遥控前进、后退、左右转和归中连续记录 30 秒，全程保持 `RUN`，假停机/恢复增量为 0；三次真实归中均在 460～534 ms 内平滑降到零命令，零命令后未自行恢复运动。

### v3.1 高精度反馈与停车尾段同步版

- 左右轮后台反馈由整数 rpm 的 `0x5000` 切换为 `0x500E`，固件保留原始 `0.1 rpm` 值；PC 工具优先使用高精度字段并把自动序列停稳门槛收紧到 `0.5 rpm`。
- 启动同步不再把一侧新反馈和另一侧旧缓存直接比较：只有左右轮各产生新样本且采样时差不超过 80 ms 才更新启动修正。
- 启动 1 秒内若一轮不高于 8 RPM 且落后至少 5 RPM，只把快轮短暂压到约 8 RPM；两轮均运动后使用更快的比例滤波，仍不增加慢轮动力、不固定补偿某一侧。
- 停车尾段把同步范围延伸到 1 RPM；当一轮已不高于 `0.5 rpm`、另一轮仍不低于 `1 rpm` 时，只把仍在运动的轮子最多削减 2 RPM，可削到零但绝不反向。
- 轮程积分补偿试验 `launch-progress-sync-rc1/rc2` 因全程残差和最坏峰值恶化而被明确否决；当前固件不保留该积分。
- 当前候选标签为 `neutral-tail-fresh-feedback-rc2`（提交 `d73fe6c`），最后一次烧录成功时间为 15:07:57，完整重编译为 `0 Error(s), 0 Warning(s)`。
- 高精度实测：±25 RPM 共 8 段，1.2 秒轮程差平均约 16.5 mm、最坏 26.0 mm，停车时差 7/8 次不超过 300 ms、单次 339 ms；±32 RPM 共 6 段，1.2 秒轮程差平均约 13.4 mm、最坏 23.1 mm，停车时差最坏 230 ms。所有有效自动测试的左右写失败、RC 停机/恢复增量均为 0。
- 转向松手后，轨迹在 259～264 ms 到零，轮速尾巴为 277/382 ms；提前 `STOP` 两次均在 512～569 ms 到零命令，零命令后旧运动命令重新出现 0 次。

### v3.2 生产清理与现场验收版

- 删除 USB `MOVE/STOP` 运动注入、禁用的台架循环、已绕过的万向轮归正状态机、主动测试 CLI 和未调用的旧电机接口；正常 SBUS、S 曲线、归中停车、直线同步、写应答及驱动器诊断参数不变。
- 电脑端工具改为纯被动遥测记录器，连接 USB CDC 不会向车辆发送运动或停止命令。

### v3.3 MC-02 BMI088 预留接口版

- 根据 MC-02 官方 `CtrBoard-H7_IMU` 例程接入板载 BMI088 的 SPI2 引脚、Mode 3/7.5 Mbit/s 配置和基础寄存器驱动。
- UART5 收敛为 SBUS 纯接收，释放复用的 PB13 给 SPI2 时钟；现有遥控接收路径保持不变。
- 新增带状态码、时间戳和样本序号的 `IMU_Init()`、`IMU_Poll()`、`IMU_CopyLatest()` 接口，默认调用保持注释。
- 暂不启用 IMU 加热、EXTI/DMA、姿态融合或 USB 遥测字段，后续按单一变量逐步开放。
- 清理固件于 16:23:30 完成烧录与校验；烧录后 55 秒急停静止压力记录中，双轮命令始终为零、反馈峰值 0.2 RPM、驱动器故障码和速度写失败均为零。
- 现场随后完成正常遥控回归，用户确认实际操作无异常，可以进入下一阶段；最终生产标签为 `smooth-control-production-v1`。
