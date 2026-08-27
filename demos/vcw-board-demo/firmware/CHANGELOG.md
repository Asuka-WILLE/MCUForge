# 更新日志

本文件记录 MCUForge 竞赛迁移期间的可审计变更。日期采用 `YYYY-MM-DD`；构建、测试、烧录结论必须以对应证据文件为准。

## [未发布] - 2026-08-27

### 新增

- 新增仓库级 `agent_infra/hiclaw/Start-MCUForge.ps1`，自动检查/启动 Docker 与 HiClaw 容器、启动两个 Bridge、注册 MCP、Bootstrap `mcuforge` Team 并打开 Element Web。
- 在 Windows 桌面新增 `MCUForge-HiClaw-一键启动.cmd`，支持双击恢复；启动日志写入 `%LOCALAPPDATA%\MCUForge\hiclaw-startup.log`，不记录密码或令牌。

### 安全边界

- 默认不启用本地自动补丁模式；脚本不会提交、推送、烧录或打开 COM。
- 只复用已运行且健康的 Bridge；缺少编译产物时才执行对应目录的 `npm ci` 和 `npm run build`。
- 控制台 API 未就绪时会等待 `/docs` 健康检查通过，避免 Docker 重启后的注册竞态。
- 桌面 `.cmd` 入口改为纯 ASCII 文本，并在 Bridge 注册前后输出分阶段状态，避免 Windows `cmd` 编码解析导致窗口闪退。

## [未发布] - 2026-08-14

### 新增

- 建立当前 STM32 工程地图：[`docs/current-project-map.md`](docs/current-project-map.md)。
- 建立现有上位机与遥测兼容性地图：[`docs/current-pc-tool-map.md`](docs/current-pc-tool-map.md)。
- 记录无源码改动的 Keil 基线构建结果：[`docs/evidence/baseline-build-2026-08-14.md`](docs/evidence/baseline-build-2026-08-14.md)。
- 增加隔离的 `mcuforge_demo` 固件模块：USB CDC 控制帧解析、虚拟左右输出、TFT Demo 页面和 JSON 遥测均不进入 SBUS/RS485 路径。
- 增加电脑端 14 字节控制协议编解码器、固定黑盒测试器，以及 `CTRL-001`、`FS-001`、`ESTOP-001`、`REC-001` 测试定义。
- 在原监控 GUI 中增加 20 ms 虚拟控制发送、油门/转向滑条、使能、停止发送断链、急停请求和归中复位控件。
- 记录 COM3 旧固件运行遥测与 TFT 照片：[`docs/evidence/hardware-runtime-baseline-2026-08-14.md`](docs/evidence/hardware-runtime-baseline-2026-08-14.md)。
- 记录 Demo 基础设施 Keil 全量构建和 Python 验证：[`docs/evidence/demo-infrastructure-build-2026-08-14.md`](docs/evidence/demo-infrastructure-build-2026-08-14.md)。
- 增加 4 个 Agent 的职责/权限合同，以及 Keil 构建、固定硬件测试、证据审计 3 个可复用 Skill 和测试用例哈希锁。
- 经用户批准烧录 Demo HEX；记录 COM3 静默遥测、`CTRL-001` 通过以及 `FS-001`、`ESTOP-001`、`REC-001` 预期失败的真实硬件证据：[`docs/evidence/demo-hardware-baseline-2026-08-14.md`](docs/evidence/demo-hardware-baseline-2026-08-14.md)。

### 已验证

- 在本地分支 `feature/mc02-bmi088-interface`、基线提交 `b4adf58` 上执行 Keil `VCW` target 构建；退出码为 0，Keil 报告为 `0 Error(s), 0 Warning(s)`。
- 记录了本次生成的 HEX 与 AXF 的 SHA-256，供后续烧录审批和证据归档比对。
- 在 `feature/mcuforge-demo-infra` 上执行新增 Demo 固件的 Keil 真实构建；报告仍为 `0 Error(s), 0 Warning(s)`。
- Python 协议、测试器和 GUI 均通过 `py_compile`；协议与测试判定单元测试 8 项全部通过，固定测试清单可正常加载。

### 约束与未做事项

- 未访问任何远端仓库，未推送、拉取或烧录固件。
- 旧 `Waiting SBUS frame...` 照片仅作为烧录前基线；当前开发板已经运行新 Demo 固件，但还需要补拍烧录后的 TFT 照片。
- 基础设施版本故意不实现 150 ms 超时、三帧中位恢复和急停锁定；固定测试应先证明这些用例失败，再交给 AgentTeams 实现。
- 未访问远端、未推送；安全功能仍未实现，不能把 `FS-001` 的预期失败描述为最终功能通过。
