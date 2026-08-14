# 当前 PC 工具与遥测兼容性地图

记录日期：2026-08-14

范围：`PC_Tools/telemetry_monitor.py` 的当前实现，不预设串口或硬件在线。

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
- 接收：后台线程持续 `readline()`，按 UTF-8 JSON 一行一帧解析。
- 记录：每次会话生成 `raw.jsonl`、`telemetry.csv` 与 `session_info.json`；无界面模式也输出包含样本数和关键统计的 JSON 摘要。
- 归一化：优先读取 `left_rpm_x10` / `right_rpm_x10`，恢复到 0.1 rpm 精度；缺失字段用 JSONL 的 `null` 和 CSV 空单元格表示，真实 `0` 保留为 `0`。
- 兼容性：未识别字段不会阻塞已知字段解析，因此在保留旧字段的前提下可增量添加竞赛遥测。

## 3. 当前遥测协议

固件在 `Core/Src/main.c:telemetry_send()` 通过 USB CDC 每约 50 ms 输出以 `\r\n` 结尾的一行 UTF-8 JSON。当前 PC 工具使用的核心字段包括：

| 分组 | 字段 |
| --- | --- |
| 时间与轮速 | `tick_ms`、`left_rpm`、`right_rpm`、`left_rpm_x10`、`right_rpm_x10`、`speed_rpm` |
| 命令与轨迹 | `left_cmd`、`right_cmd`、`cmd_valid`、`target_linear`、`target_steer`、`conditioned_linear`、`conditioned_steer`、`traj_*` |
| 同步与写入回执 | `sync_*`、`motor_write_sequence`、`left_write_echo_ok`、`right_write_echo_ok`、`*_write_fail_count` |
| SBUS 与状态 | `rc_ready`、`rc_age_ms`、`rc_link_age_ms`、`rc_*`、`sbus_failsafe`、`state` |
| 反馈与诊断 | `height_mm`、`*_speed_age_ms`、`left/right_torque`、`*_fault_code`、`*_enable`、`*_diagnostic_*`、`*_health_age_ms` |

## 4. 与竞赛基线的缺口

当前工具只读取遥测，代码中没有控制帧打包、`serial_port.write()`、虚拟手柄、故障注入或固定 FS-001 至 FS-007 CLI 测试。后续 PC Tool Agent 必须在保留现有接收、图表和记录能力的基础上增加：

1. 20 ms 定时的 14 字节控制帧发送器（版本、序号、`int16` 油门/转向、flags、Modbus CRC16）。
2. 鼠标二维摇杆、W/A/S/D、回中、使能、急停、开始/停止发送界面。
3. 丢帧、CRC 错误、越界、重复/倒退序号、延迟/抖动、非中立恢复和急停故障注入。
4. 独立 CLI 固定测试器；测试预期置于只读测试用例文件，功能实现 Agent 不得修改。
5. 同时兼容现有遥测字段，并新增 `input_age_ms`、`safety_state` 与 `failure_reason` 等字段的显示和记录。

在 USB CDC 接收解析与虚拟输出隔离尚未完成前，严禁让该工具向当前工程发送控制帧。
