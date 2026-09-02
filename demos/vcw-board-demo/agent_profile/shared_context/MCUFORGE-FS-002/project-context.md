# MCUForge-FS-001 工程事实

## 1. 项目与硬件边界

- 仓库：`MCUForge`；任务基线提交：`efba900`；固件根目录：`demos/vcw-board-demo/firmware`。
- MCU：STM32H723VET6；Keil 工程：`MDK-ARM/VCW.uvprojx`；Target：`VCW`。
- 比赛模式：`MCUFORGE_DEMO_MODE=1` 时，主循环使用 `MCUForge_Demo_Process()`；不进入旧 SBUS、RS485 查询或真实电机写入流程。
- Demo 只验证开发板、TFT、USB CDC、PC 虚拟控制和可审计证据。虚拟左右输出不能写入真实 RS485 电机链路。

## 2. 已有通信与显示能力

- USB CDC 接收受限的 14 字节 PC 控制帧；帧含 CRC、序号、油门、转向、使能和急停标志，协议实现位于 `PC_Tools/mcuforge_protocol.py`。
- 设备约每 50 ms 通过 USB CDC 输出一行 JSON 遥测；PC 监控工具可显示数值、曲线、虚拟手柄并保存日志。
- `Core/Src/mcuforge_demo.c` 已解析帧、计算虚拟左右输出、更新 TFT 和 JSON 遥测。
- 当前基线 TFT 明确显示 `BASELINE: NO FAILSAFE`；它是本 run 要消除的功能缺口，不是可接受状态。

## 3. 任务相关文件

| 文件 | 当前作用 | 本 run 规则 |
| --- | --- | --- |
| `Core/Src/mcuforge_demo.c` | Demo 状态、输入解析、虚拟混控、TFT 与遥测 | Firmware 可修改。 |
| `Core/Inc/mcuforge_demo.h` | Demo 对外接口 | Firmware 可修改。 |
| `Core/Src/main.c` | 切换 Demo/生产控制路径 | 默认不改；需人类批准。 |
| `USB_DEVICE/App/usbd_cdc_if.c` | USB CDC 接收回调 | 默认不改；需人类批准。 |
| `PC_Tools/mcuforge_protocol.py` | 电脑控制帧生成与解码 | 默认不改；需人类批准。 |
| `PC_Tools/mcuforge_testcases/*.json` | 固定黑盒验收用例 | 永远不可修改。 |

## 4. 已知基线与未完成项

- `CTRL-001` 已证明 PC 控制帧到虚拟混控链路可用。
- `FS-001`、`ESTOP-001`、`REC-001` 在基线中按预期失败，原因是断链、急停锁定和三帧中位恢复尚未实现。
- 已有真实 Keil 构建证据为 `0 Error(s), 0 Warning(s)`；构建成功不能代替烧录和硬件通过。

## 5. 禁止推断

- 不把生产车控中的 SBUS 逻辑直接复制到 Demo 状态机；输入来源和安全边界不同。
- 不把“电脑未发送帧”误判为 USB 断开或 MCU 复位；必须以冻结用例的时间和状态断言为准。
- 不以改长等待时间、删除断言或修改测试 JSON 来获得通过。
- 不对 BMI088、TFT 控制器型号、引脚或寄存器做无来源猜测；本 run 没有要求修改这些内容。
