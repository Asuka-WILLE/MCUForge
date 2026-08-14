# 当前工程地图（MCUForge 迁移基线）

记录日期：2026-08-14

范围：仅描述当前工作树；不对外部仓库、远端或硬件连接状态作推断。

## 1. 仓库基线

| 项目 | 已确认值 |
| --- | --- |
| 仓库根目录 | `C:\Users\hz_wu\Desktop\GOAI\wheel_control-main\wheel_control-main` |
| 本地分支 | `feature/mc02-bmi088-interface` |
| 基线提交 | `b4adf58` — `feat: reserve MC02 BMI088 IMU interface` |
| 初始工作树 | 干净；本次开始前没有未提交改动 |
| Git 使用范围 | 仅本地检查、暂存与提交；未执行网络 Git 操作 |

## 2. 构建工程

| 项目 | 已确认值 |
| --- | --- |
| CubeMX 配置 | `UM10550.ioc` |
| MCU | `STM32H723VET6`（CubeMX 名称 `STM32H723VETx`，LQFP100） |
| 系统时钟 | HSE 24 MHz；CPU / Cortex 时钟 480 MHz；HCLK 240 MHz |
| Keil 工程 | `MDK-ARM/UM10550.uvprojx` |
| Keil target | `UM10550` |
| 工具链配置 | MDK-ARM V5.32 工程；本机实际构建使用 µVision 5.40 与 ArmClang 6.22 |
| 产物配置 | 启用 HEX 输出；路径 `MDK-ARM/UM10550/UM10550.hex` |

## 3. 硬件与通信事实

| 用途 | 实现位置 | 当前配置 / 边界 |
| --- | --- | --- |
| SBUS 遥控输入 | `Core/Src/SBUS.c`、`Core/Src/usart.c` | UART5，PD2 RX，100000 bps、8E2、仅接收；由 `SBUS_TimeoutCheck()` 参与安全判断。 |
| 左右轮驱动 RS485 | `Core/Src/usart.c` | USART2，PD5/PD6，115200 bps、8N1；`RS485_SendPacket*()` 控制 PD4 DE。现有路径会对真实轮毂电机发命令。 |
| 第二路 RS485 | `Core/Src/usart.c` | USART3，PD8/PD9，9600 bps、8N1；`RS485_SendPacket2*()` 控制 PB14 DE。 |
| 调试串口 | `Core/Src/usart.c` | USART1，PA9/PA10，115200 bps、8N1。 |
| USB CDC | `USB_DEVICE/`、`Core/Src/main.c` | 以约 50 ms 周期向电脑发送 JSON 遥测。现有 `CDC_Receive_HS()` 只重新挂起接收缓冲区，未解析或分派 PC 控制数据。 |
| TFT | `Core/Src/lcd.c`、`Core/Inc/lcd.h` | SPI1 为显示写入，PB10 背光、PB11 复位、PD10 数据/命令；当前 LCD 显示遥控输入调试信息。 |
| IMU | `Core/Src/imu.c`、`Core/Inc/imu.h` | BMI088 使用 SPI2；本次迁移不启用或修改 IMU 逻辑。 |

## 4. 当前控制与安全逻辑

`Core/Src/main.c` 仍是现有车控逻辑的聚集点，包含轨迹、同步、SBUS 可信度、LCD 调试、轮毂写入与遥测状态机。关键入口如下：

- `motor_speed_control_update()`：将计算出的左右命令交给实际电机写入流程。
- `motor_speed_set_confirmed()` 与 `motor_write_speed_with_echo()`：带回读确认的真实轮毂电机命令。
- `rc_frame_trust_result()`、`rc_accept_trustworthy_frame()`、`rc_safety_stop_update()`：现有 SBUS 输入可信度与停机处理。
- `SBUS_Receive()`、`SBUS_TimeoutCheck()`：UART5 SBUS 帧接收、解析和超时。
- `telemetry_process()`：后台查询加约 50 ms USB JSON 遥测发送。

`Core/Src/move_mode_control.c` 提供 Modbus CRC、轮毂电机启停、升降机构等函数；`Core/Src/usart.c` 封装 RS485 收发。它们是生产车控的既有能力，不能在竞赛 Demo 中直接作为“虚拟左右轮”输出使用。

## 5. 竞赛迁移差异与后续边界

比赛 Demo 只验证真实 STM32 固件与电脑—USB CDC 链路，不验证真实电机或车辆。因此当前工程与目标基线存在三个明确差异：

1. USB CDC 有接收回调但没有控制帧解析；需新增受限的 14 字节控制帧解析模块。
2. 当前 `main.c` 中控制逻辑过于集中；新的 PC 输入、协议校验、虚拟左右轮计算、failsafe 状态机必须拆为独立模块，不能继续堆入 `main.c`。
3. 现有输出会写入真实 RS485 驱动器；比赛路径必须实现与该路径隔离的虚拟命令、TFT 显示和遥测字段，未完成隔离前禁止发送竞赛控制帧。

在完成上述隔离、固定测试器和 `FS-001` 的真实失败证据前，不实现最终 150 ms failsafe 功能，也不创建 `baseline-no-failsafe` 标签。
