# MCUForge Demo 硬件基线证据（2026-08-14）

## 结论

经用户明确批准，`feature/mcuforge-demo-infra` 的 Demo 固件已通过 CMSIS-DAP 烧录到 STM32 开发板。COM3 新遥测和固定黑盒测试证明：

- USB CDC 控制帧、虚拟混控、TFT/遥测 Demo 路径已实际运行；
- `CTRL-001` 通过；
- `FS-001` 按预期失败，证明基础版本确实没有 150 ms 失联清零；
- `ESTOP-001` 与稳定化后的 `REC-001` 按预期失败，分别证明急停锁定和三帧中位恢复尚未实现；
- 测试过程中只更新虚拟变量，没有进入 SBUS、RS485 或电机控制路径。

## 烧录身份

| 项目 | 值 |
| --- | --- |
| 源提交 | `fdc011a63d4996920894c1d1e9e46b153998e3fc` |
| Keil target | `VCW` |
| HEX SHA-256 | `4F4FA7C1F67FB8EFC39907D3E645E61A05FBFF6D093134995EB7E98531CDD910` |
| 调试器 | `Horco CMSIS-DAP v2`，`VID_FAED&PID_4870` |
| CDC 端口 | COM3，`VID_0483&PID_5740` |
| 烧录完成时间 | 2026-08-14 21:13:07 |

Keil 下载日志：

```text
Load "VCW\VCW.axf"
Erase Done.
Programming Done.
Verify OK.
Application running ...
Flash Load finished at 21:13:07
```

日志 SHA-256：`2CC7CED9A5BA7E63EAC42F4962D5A8FEEEADCA48B01BBBD91A61996A20824460`。

## 烧录后静默检查

在 COM3 连续读取 5 秒：

- 有效 JSON 遥测 101 条；
- 状态集合仅为 `DEMO_BASELINE`；
- `left_cmd=0`、`right_cmd=0`；
- CRC、范围、序号和接收溢出计数均为 0；
- 证明旧 `FAILSAFE / Waiting SBUS frame...` 固件已被替换为 MCUForge Demo 固件。

本地完整会话：`PC_Tools/data/post-flash-smoke-2026-08-14/2026-08-14_21-13-25`。该目录按设计被 Git 忽略；其三份文件哈希如下：

| 文件 | SHA-256 |
| --- | --- |
| `raw.jsonl` | `1C844D2C612CDC557B923234E2E03BA902DD9C29611DDA95406E3FD8AFE6A7D2` |
| `telemetry.csv` | `5C2335B37A1FC3D8ED6C96B2F29B4A2D83D2CE1EC66274D1D78B8AB56CE4A577` |
| `session_info.json` | `5601ADA69BE6DF7D88F180092D85F42EB39C0DD293033B18D70713FC63817C91` |

## CTRL-001：通过

- 时间：2026-08-14 21:14:02
- 发送控制帧：25
- 有效遥测：7
- 输入：`throttle=400`、`steering=100`
- 实测：`left_cmd=500`、`right_cmd=300`、`cmd_valid=1`
- 断言结果：PASS

## FS-001：预期失败

- 时间：2026-08-14 21:14:10
- 发送控制帧：15，随后停止发送 350 ms
- 有效遥测：12
- 在 `pc_frame_age_ms=169、219、269、319、369` 时，固件仍保持：
  - `left_cmd=600`
  - `right_cmd=600`
  - `cmd_valid=1`
  - `state="DEMO_BASELINE"`
- 固定预期为清零并进入 `FAILSAFE`，因此断言结果为 FAIL，退出码为 1。

这不是测试器故障，而是为 AgentTeams 保留的真实实现缺口。

## ESTOP-001：预期失败

- 时间：2026-08-14 21:16:22
- 急停阶段仍回传 `left_cmd=500`、`right_cmd=500`、`cmd_valid=1`；
- 状态仍为 `DEMO_BASELINE`，没有进入 `ESTOP`；
- 断言结果为 FAIL，退出码为 1。

## REC-001：预期失败

第一次执行发现 80 ms 的观察窗口可能收不到 50 ms 遥测。基线冻结前将用例改为“40 ms 单帧发送 + 90 ms 独立观察”，三帧之间仍小于 150 ms；同时增加 `pc_recovery_neutral_count=2` 断言。该修改只消除采样抖动，没有放宽安全要求，测试锁更新为 `C63037AEB57245047FE60DBD8E0170AF371F35C30A21FD2FDDFB3F3E535A1168`。

稳定版本于 2026-08-14 21:17:33 执行：

- 第二个中位帧后状态仍错误地为 `DEMO_BASELINE`，且缺少恢复计数字段；
- 第三个中位帧后仍没有进入 `RUN` 或 `DISABLED`；
- 后续运动帧能生效，但状态仍错误地为 `DEMO_BASELINE`；
- 三项断言均失败，退出码为 1。

## 已归档的原始证据

| 文件 | SHA-256 |
| --- | --- |
| [`CTRL-001-result.json`](runtime/2026-08-14-demo-baseline/CTRL-001-result.json) | `8FDEF114A6B3B4B4B96EF8869301805B668EFD37177BDC2C0396B6C73DEC2575` |
| [`CTRL-001-telemetry.jsonl`](runtime/2026-08-14-demo-baseline/CTRL-001-telemetry.jsonl) | `14AAEB9543127B0AE24A0644830B1985BA9B99F8A4B93DA69BE94FF3B640F56B` |
| [`ESTOP-001-result.json`](runtime/2026-08-14-demo-baseline/ESTOP-001-result.json) | `43C0F25CFEADACF63C391DC60CB818ABE9022C0D04AFE35F54808416CE0D40EB` |
| [`ESTOP-001-telemetry.jsonl`](runtime/2026-08-14-demo-baseline/ESTOP-001-telemetry.jsonl) | `F42014518D6B093C829896D24A40A7F591D5E6A9AE12D1242869A2E2CE6E3C42` |
| [`FS-001-result.json`](runtime/2026-08-14-demo-baseline/FS-001-result.json) | `3C695F2E8ED7BDFDA2F1B9CA617F38C7BF6F6E607FB9BC07BD3B6930BD4C4EE0` |
| [`FS-001-telemetry.jsonl`](runtime/2026-08-14-demo-baseline/FS-001-telemetry.jsonl) | `73701CBB6BD352B3A6411A444D178E884825436544BB5337491490B10AAD9407` |
| [`REC-001-result.json`](runtime/2026-08-14-demo-baseline/REC-001-result.json) | `AF458C92C573979A6872F06FBEC2AD0D4ED6BA3B99E296055081C0D3E4E7464E` |
| [`REC-001-telemetry.jsonl`](runtime/2026-08-14-demo-baseline/REC-001-telemetry.jsonl) | `4738C60A643154B53ED5E42F77D1188EE1EDE29A9DF88467D1C4A2096DDFCE09` |

## 尚缺的视觉证据

需要补拍一张开发板当前 TFT 页面照片，画面应包含 `MCUFORGE DEMO BASE`，用于把实物显示与本次 HEX/COM3 会话关联起来。该照片不影响上述串口黑盒结论，但比赛演示材料需要它。
