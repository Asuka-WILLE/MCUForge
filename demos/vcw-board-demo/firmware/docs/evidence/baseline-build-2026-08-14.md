# 无修改基线构建证据

## 运行信息

| 项目 | 值 |
| --- | --- |
| 日期 | 2026-08-14 |
| 工作目录 | `C:\Users\hz_wu\Desktop\GOAI\wheel_control-main\wheel_control-main` |
| 本地分支 | `feature/mc02-bmi088-interface` |
| 基线提交 | `b4adf58` |
| 运行命令 | `C:\Users\hz_wu\AppData\Local\Keil_v5\UV4\UV4.exe -b MDK-ARM\VCW.uvprojx -t VCW` |
| 开始时间 | 2026-08-14T19:50:36.3447325+08:00 |
| 结束时间 | 2026-08-14T19:50:40.7019073+08:00 |
| 进程退出码 | `0` |

## Keil 报告摘录

Keil 生成的完整原始报告保留在本机忽略目录：

`MDK-ARM/VCW/VCW.build_log.htm`

该报告由本次命令在 2026-08-14 19:50:40 更新，构建结果为：

```text
Toolchain:       MDK-ARM Professional Version: 5.40.0.3
C Compiler:      ArmClang.exe V6.22
Build target 'VCW'
"VCW\VCW.axf" - 0 Error(s), 0 Warning(s).
Build Time Elapsed: 00:00:01
```

完整报告可能包含本机许可证信息，故不纳入版本库；其路径、更新时间、命令、退出码和不可变产物哈希已在本文件登记。

## 构建产物哈希

| 产物 | SHA-256 |
| --- | --- |
| `MDK-ARM/VCW/VCW.hex` | `73F6BF06FEF6981265BFE460CF15F7F0DD42F511BBF2688A2313E4B38A9E068E` |
| `MDK-ARM/VCW/VCW.axf` | `8203B7C5DAF54A1DC749178CF0917BE18A97DCF87FBD24ACEFBF131BB3EE438C` |

## 结论与边界

- 此次为真实 Keil 命令行构建，基线编译通过。
- 未连接或检测硬件，未烧录，未启动 PC 监控，也没有执行串口/FS 测试；因此本记录不宣称任何运行时、安全或 TFT 验证结论。
- `MDK-ARM/VCW/` 是项目既有的忽略构建目录；本次生成的 `.dep` 与 `.build_log.htm` 均未纳入 Git。
