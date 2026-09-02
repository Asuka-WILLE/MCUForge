# MCUForge 当前验证证据

本文记录 2026-09-03 交接版本实际执行过的检查。它区分代码检查、运行态集成、
项目房间闭环和硬件验证，避免把“脚本能运行”写成“所有硬件都已通过”。

## 1. 验证环境

- Windows + PowerShell 7.6.4；
- Docker Desktop 与本机 HiClaw Controller；
- Node.js v24.14.1；
- MCUForge Team：Manager、Lead、Requirement、Research、Firmware、Verification；
- STM32 Bridge `8765`、Research Bridge `8766`、Worker Control Bridge `18765`。

API Key、Matrix token、密码和本机环境文件均未写入仓库。

## 2. 静态与构建检查

执行：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Test-MCUForge.ps1 -Mode Static
```

结果：通过。

- 32 个 PowerShell 脚本通过解析；
- Worker Control Bridge 的 Python 源码通过 AST 解析；
- 项目房间 Shell 脚本通过 `bash -n`；
- STM32 MCP Server 的 TypeScript 构建通过；
- Research MCP Server 的 TypeScript 构建通过；
- `git diff --check` 通过。

这里的 TypeScript 构建只证明 Bridge 源码可编译，不等于 STM32 固件已构建。

## 3. 真实消息链路验收

执行：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Test-MCUForge.ps1 -Mode Full -CoordinationRounds 2
```

结果：通过。

| 项目 | 结果 |
| --- | --- |
| STM32 Bridge 健康检查 | 通过 |
| Research Bridge 健康检查 | 通过 |
| HiClaw Team | `Active`、Lead Ready、Workers 4/4 |
| Worker 协调检查 | 8/8 次成功 |
| Worker 最大接单延迟 | 5752 ms |
| Manager 最大跟进延迟 | 48 ms |
| 最终事件与真实 `m.mentions` | 8/8 次通过 |

这不是只看容器 `Running`；测试向四个 Worker 发送真实 Matrix 消息，并验证 Worker
最终回执和 Manager 的确定性跟进事件。

## 4. 自然语言创建项目闭环

测试从 `Manager: default` 私聊发送一句自然语言新建项目请求，不直接调用创建脚本。
Manager 自动创建独立房间，测试结果如下：

| 闸门 | 结果 |
| --- | --- |
| 生命周期 | `READY` |
| 交互模式 | `project_room_only` |
| 预期/实际成员 | 7/7 |
| 原始请求事件 | 可见且事件 ID 已登记 |
| 来源房间迁移通知 | 可见且事件 ID 已登记 |
| `[PROJECT_CREATED]` 轨迹 | 可见 |
| Manager 房间配置 | 生效 |
| MinIO 元数据 | 与本地一致 |
| 审计事件 | 7 条 |

随后主动重启 `hiclaw-manager`，再次运行项目房间专项测试，以上闸门仍全部通过。
这验证了项目房间配置会持久化，而不是只在创建瞬间短暂可见。

本次同时修复了一个真实异常：Worker 邀请后自动入房存在短暂延迟，旧流程会因临时
拿不到 Worker token 而提前进入 `BLOCKED`，随后模型手工恢复又可能漏记原始请求
事件。新流程会观察真实成员状态，并在进入 `READY` 前强制校验来源事件、请求哈希、
原始请求事件、迁移通知和 `request.md`；恢复只能用同一项目 ID 幂等重跑脚本。

验证创建的 5 个临时项目房间已逐一标记为 `TERMINATED`，管理员和 Agent 均已退出；
`meta.json` 与 `audit.ndjson` 仍保留在 MinIO，可用于复盘，未被删除。

## 5. 环境检查与启动

以下入口均已实际执行并通过：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Check-MCUForgeEnvironment.ps1
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Start-MCUForge.ps1 -SkipDependencyBuild -NoBrowser
```

环境检查确认 PowerShell 7、Git、Docker、Node.js、npm、HiClaw Controller、环境文件和
仓库关键文件可用；启动入口完成 Bridge 注册、Team 恢复与 4/4 Worker 就绪检查。

## 6. 本次没有验证什么

- 未在交接收尾阶段重新执行 VCW 固件的真实 Keil 全量构建；
- 未烧录开发板；
- 未打开或操作 COM 口；
- 未进行 TFT、USB CDC、虚拟手柄的现场硬件验收；
- 未验证其他芯片/开发板 Profile；
- 未把本地模型账号和 API Key 打入代码包。

因此，当前结论是“MCUForge Infra、Bridge 构建、HiClaw 消息链路和项目房间闭环已
通过本机验收”，不是“任意固件或硬件已经通过”。VCW 固件的历史构建和 Demo 证据
位于 `demos/vcw-board-demo/agent_profile/`，接手者应在自己的开发板环境中重新验证。

## 7. 接手者复现命令

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Check-MCUForgeEnvironment.ps1
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Install-MCUForge.ps1
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Start-MCUForge.ps1
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Test-MCUForge.ps1 -Mode Static
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Test-MCUForge.ps1 -Mode Live
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Test-MCUForge.ps1 -Mode Full -CoordinationRounds 1
```

任何一步失败都应保留原始输出，并按 [运行与排障手册](OPERATIONS.md) 定位；不要通过
删除测试、放宽路径白名单或直接改状态文件来制造“通过”。
