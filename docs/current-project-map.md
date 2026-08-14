# 当前工程地图（MCUForge 迁移基线）

记录日期：2026-08-14

范围：仅描述当前工作树；不对外部仓库、远端或硬件连接状态作推断。

## 1. 仓库基线

| 项目 | 已确认值 |
| --- | --- |
| 仓库根目录 | `C:\Users\hz_wu\Desktop\GOAI\wheel_control-main\wheel_control-main` |
| 本地分支 | `feature/mcuforge-demo-infra` |
| 迁移基础提交 | `dbca8f7` — `docs: record MCUForge migration baseline` |
| 分支用途 | 只构建开发板、TFT、USB CDC 和电脑虚拟控制的比赛基础设施 |
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
| USB CDC | `USB_DEVICE/`、`Core/Src/mcuforge_demo.c` | Demo 源码接收 14 字节 PC 控制帧，并以约 50 ms 周期发送虚拟状态 JSON；旧硬件固件尚未烧录。 |
| TFT | `Core/Src/lcd.c`、`Core/Src/mcuforge_demo.c` | Demo 源码显示 PC 输入、虚拟左右输出和“无 failsafe 基线”；旧硬件照片仍是 SBUS 调试页。 |
| IMU | `Core/Src/imu.c`、`Core/Inc/imu.h` | BMI088 使用 SPI2；本次迁移不启用或修改 IMU 逻辑。 |

## 4. 当前控制与安全逻辑

生产车控代码仍保留在 `Core/Src/main.c`，但 `MCUFORGE_DEMO_MODE=1` 时主循环只调用 `MCUForge_Demo_Process()`，因此不会进入 SBUS、RS485 查询或电机写入。旧路径的关键入口如下：

- `motor_speed_control_update()`：将计算出的左右命令交给实际电机写入流程。
- `motor_speed_set_confirmed()` 与 `motor_write_speed_with_echo()`：带回读确认的真实轮毂电机命令。
- `rc_frame_trust_result()`、`rc_accept_trustworthy_frame()`、`rc_safety_stop_update()`：现有 SBUS 输入可信度与停机处理。
- `SBUS_Receive()`、`SBUS_TimeoutCheck()`：UART5 SBUS 帧接收、解析和超时。
- `telemetry_process()`：后台查询加约 50 ms USB JSON 遥测发送。

`Core/Src/move_mode_control.c` 提供 Modbus CRC、轮毂电机启停、升降机构等函数；`Core/Src/usart.c` 封装 RS485 收发。它们是生产车控的既有能力，不能在竞赛 Demo 中直接作为“虚拟左右轮”输出使用。

## 5. 竞赛迁移差异与后续边界

比赛 Demo 只验证真实 STM32、TFT 与电脑 USB CDC 链路，不验证电机或车辆。当前基础设施已经完成：

1. 受限的 14 字节控制帧解析、CRC/范围/序号校验；
2. 与生产车控隔离的虚拟左右输出、独立 TFT 页面和 JSON 遥测；
3. 电脑虚拟手柄和固定 CLI 黑盒测试。

当前故意保留的任务缺口是 150 ms 超时、三帧中位恢复、急停锁定及相应状态显示。新固件尚未烧录；在获得用户许可并记录 `FS-001` 真实失败前，不创建 `baseline-no-failsafe` 标签。
