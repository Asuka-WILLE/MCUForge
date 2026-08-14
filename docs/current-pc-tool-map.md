# 当前 PC 工具与遥测兼容性地图

记录日期：2026-08-14

范围：`PC_Tools/telemetry_monitor.py`、控制协议和固定测试器的当前开发分支实现。

## 1. 入口和依赖

| 项目 | 已确认值 |
| --- | --- |
| 主程序 | `PC_Tools/telemetry_monitor.py` |
| Python 依赖 | `pyserial>=3.5`、`matplotlib>=3.7`；GUI 使用标准库 Tkinter |
| 默认串口速率 | 115200 bps，与 USB CDC 遥测一致 |
| GUI 类 | `TelemetryMonitor(tk.Tk)` |
| 无界面入口 | `python PC_Tools\telemetry_monitor.py --headless --port COMx --duration 30` |

## 2. 现有能力

- GUI：枚举串口、连接/断开、实时数值卡片与左轮、右轮、车速曲线。
- 操作：油门/转向滑条、使能、20 ms 周期发送、停止发送模拟断链、急停请求和归中复位。
- 接收：后台线程持续 `readline()`，按 UTF-8 JSON 一行一帧解析。
- 记录：每次会话生成 `raw.jsonl`、`telemetry.csv` 与 `session_info.json`；无界面模式也输出包含样本数和关键统计的 JSON 摘要。
- 归一化：优先读取 `left_rpm_x10` / `right_rpm_x10`，恢复到 0.1 rpm 精度；缺失字段用 JSONL 的 `null` 和 CSV 空单元格表示，真实 `0` 保留为 `0`。
- 兼容性：未识别字段不会阻塞已知字段解析，因此在保留旧字段的前提下可增量添加竞赛遥测。
- 固定测试：`mcuforge_test_runner.py` 读取只读 JSON 用例，发送控制阶段、记录逐条遥测并生成机器可读 `result.json`。

## 3. 当前遥测协议

固件在 `Core/Src/main.c:telemetry_send()` 通过 USB CDC 每约 50 ms 输出以 `\r\n` 结尾的一行 UTF-8 JSON。当前 PC 工具使用的核心字段包括：

| 分组 | 字段 |
| --- | --- |
| 时间与轮速 | `tick_ms`、`left_rpm`、`right_rpm`、`left_rpm_x10`、`right_rpm_x10`、`speed_rpm` |
| 命令与轨迹 | `left_cmd`、`right_cmd`、`cmd_valid`、`target_linear`、`target_steer`、`conditioned_linear`、`conditioned_steer`、`traj_*` |
| 同步与写入回执 | `sync_*`、`motor_write_sequence`、`left_write_echo_ok`、`right_write_echo_ok`、`*_write_fail_count` |
| SBUS 与状态 | `rc_ready`、`rc_age_ms`、`rc_link_age_ms`、`rc_*`、`sbus_failsafe`、`state` |
| 反馈与诊断 | `height_mm`、`*_speed_age_ms`、`left/right_torque`、`*_fault_code`、`*_enable`、`*_diagnostic_*`、`*_health_age_ms` |
| MCUForge Demo | `demo_mode`、`input_source`、`pc_*`、`left_cmd`、`right_cmd`、`cmd_valid`、`state` |

## 4. 固定测试与剩余缺口

当前固定用例包括：

1. `CTRL-001`：证明 PC 控制帧和虚拟混控链路可用；
2. `FS-001`：断帧超过 150 ms 后输出必须清零并进入 `FAILSAFE`；
3. `ESTOP-001`：急停请求必须立即清零并锁定 `ESTOP`；
4. `REC-001`：`FAILSAFE` 后连续三帧中位才能恢复。

基础固件已在 COM3 实测：`CTRL-001` 通过，`FS-001`、`ESTOP-001`、`REC-001` 按预期失败。后续还可增加 CRC 错误、越界、重复/倒退序号和抖动测试，但不能修改现有用例来迎合实现。
