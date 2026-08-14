# MCUForge USB 虚拟控制协议

该协议只服务“开发板 + TFT + 电脑上位机”比赛 Demo。固件启用 `MCUFORGE_DEMO_MODE` 后，USB CDC 控制帧只更新虚拟左右输出，不调用 SBUS、RS485 或实际电机控制。

## 控制帧

每帧固定 14 字节，电脑默认每 20 ms 发送一次。多字节整数均为小端序。

| 偏移 | 长度 | 字段 | 约束 |
| ---: | ---: | --- | --- |
| 0 | 2 | 帧头 | `AA 55` |
| 2 | 1 | 协议版本 | `01` |
| 3 | 1 | 帧类型 | `01` 控制帧 |
| 4 | 2 | 序号 | `uint16`，按回绕顺序递增 |
| 6 | 2 | 油门 | `int16`，范围 `-1000..1000` |
| 8 | 2 | 转向 | `int16`，范围 `-1000..1000` |
| 10 | 1 | 标志 | bit0 使能；bit1 急停请求 |
| 11 | 1 | 保留 | 必须为 0 |
| 12 | 2 | CRC16 | Modbus CRC，覆盖字节 0..11 |

虚拟混控规则：`left = clamp(throttle + steering)`，`right = clamp(throttle - steering)`，结果限制在 `-1000..1000`。

同一发送会话内，序号必须递增；若 150 ms 没有接受到有效帧，解析器允许新的电脑进程从任意序号建立新会话。该重同步只影响序号校验，基础版本仍会故意保持上一次虚拟输出，不会因此自动清零。

## 基础设施基线的有意缺口

当前基础版本会校验帧头、版本、CRC、范围和序号，但故意不实现以下比赛目标：

- 最后一个有效控制帧超过 150 ms 后进入 `FAILSAFE` 并清零；
- `FAILSAFE` 后必须连续收到 3 个有效中位帧才能恢复；
- 急停请求立即锁定 `ESTOP` 并清零；
- 上位机和 TFT 显示上述安全状态。

这些缺口由 `FS-001`、`REC-001`、`ESTOP-001` 固定黑盒测试暴露，作为后续 AgentTeams 的实际编码任务。`CTRL-001` 用于证明基础 USB 控制和虚拟混控链路已经可用。

## 测试命令

先列出测试：

```powershell
python PC_Tools\mcuforge_test_runner.py --list
```

固件烧录并重新枚举为 COM3 后运行单项测试：

```powershell
python PC_Tools\mcuforge_test_runner.py --port COM3 --case CTRL-001
python PC_Tools\mcuforge_test_runner.py --port COM3 --case FS-001
```

每次运行会在 `PC_Tools/data/mcuforge-tests/` 下生成 `result.json` 和逐条遥测 `telemetry.jsonl`。该目录被 Git 忽略，只作为本机实测证据。
