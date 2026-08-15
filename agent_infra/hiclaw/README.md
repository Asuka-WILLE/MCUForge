# MUC_AGENT HiClaw Team

本目录保存 MUC_AGENT 在 HiClaw（AgentTeams 原名）中的可复现团队运行时。它面向两类任务：接手已有 MCU 工程，以及从器件和需求出发建立新工程；具体角色与冻结上下文由 `demos/<demo>/agent_profile/` 提供，而不与平台代码混放。

## 团队

| Worker | 身份 | 主要交付物 |
| --- | --- | --- |
| `mcuforge-lead` | Team Leader | 任务状态、拆解、交接和最终结论；使用 HiClaw 内置 Team Leader 编排模板 |
| `mcuforge-requirements` | Requirement & Architecture | 冻结的验收合同 |
| `mcuforge-research` | Research & Knowledge | 来源清单、事实卡片、许可证结论 |
| `mcuforge-firmware` | Firmware Engineer | 受限源码变更和构建摘要 |
| `mcuforge-verification` | Verification & Evidence | 独立验证和证据报告 |

核心角色默认使用 `deepseek-v4-pro`。当前 HiClaw 版本由 Team 资源管理 Leader，普通 Worker 接口不能覆盖其 SOUL；UM10550 示例专属规则位于 `demos/um10550-board-demo/agent_profile/hiclaw_roles/`，并在任务启动时作为团队协议传入。Worker 只通过结构化交接物协作，不能用聊天结论代替合同、来源、构建或测试证据。

## 创建或更新

从仓库根目录执行：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File agent_infra\hiclaw\Bootstrap-MCUForgeTeam.ps1
```

脚本是幂等的：已存在的 Team/Worker 会被更新，不会重复创建。它不会烧录、推送或删除任何仓库文件。

## 安装自定义工程 Skills

`agent_infra/skills/general-engineering-principles-2026-08-16/` 中的九个 `.skill` 包已分配给 Team Leader 与四个 Worker。它们覆盖证据驱动调试、接口契约、回归安全、状态机、时序、验证、交付和文档等通用工程原则；不增加任何 MCP、Shell、烧录或 Git 写入权限。

每个包会在安装时校验 ZIP 结构、YAML `name`/`description` 和 SHA-256，再解包为 Worker 工作区的 `skills/<name>/SKILL.md`。内容持久化在 HiClaw MinIO 中，Worker 重启或重新创建后仍可同步恢复。`Bootstrap-MCUForgeTeam.ps1` 默认会执行安装；只想更新 Team 配置时可加 `-SkipBundledSkills`。

需要单独重装或更新这些包时，从仓库根目录执行：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File agent_infra\hiclaw\Install-MCUForgeSkills.ps1
```

安装脚本不删除已有的其他自定义 Skill；它只覆盖同名的九个 Skill，并逐一检查五个角色的本地工作区是否已经出现对应 `SKILL.md`。

## 发布冻结任务上下文

任务开始前，先把已提交的工程事实、验收合同和来源清单发布到 Team 共享存储：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File agent_infra\hiclaw\Publish-MCUForgeSharedContext.ps1
```

默认发布 `MCUFORGE-FS-002`。脚本拒绝从脏工作树发布，记录内容哈希，并拒绝用相同 `run_id` 覆盖不同上下文；要修改合同，必须创建新的 run。四个 OpenClaw Worker 使用 `/root/hiclaw-fs/shared/mcuforge/runs/<run_id>/`，Leader 也同步至同一路径。

## 当前接入状态

已完成：

1. `mcuforge` Team 与 Leader、Requirement、Research、Firmware、Verification 五个角色运行；
2. Windows STM32 Tool Bridge 通过 Higress MCP Proxy 接入 Firmware/Verification；
3. Research Web Bridge 通过 Higress MCP Proxy 仅接入 Research Worker：它只能搜索和读取允许名单内的公开 HTTPS 技术页面，禁止登录、下载、执行、私网访问、仓库写入；
4. Worker 已端到端调用工程快照、固定测试哈希审计和真实 Keil 全量构建；
5. 所有 Windows 脚本统一使用 PowerShell 7（`pwsh`）；
6. 烧录、COM 口、源码写入和远程推送仍未开放。

研究桥的本机服务先启动一次，再配置网关和 Worker：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File agent_infra\tool_bridge\research-web-mcp-server\Start-ResearchWebBridge.ps1
pwsh -NoProfile -ExecutionPolicy Bypass -File agent_infra\tool_bridge\research-web-mcp-server\Configure-HiClawResearchProxy.ps1
```

下一阶段：设计只能修改白名单文件且可审计回滚的补丁通道。烧录和远程推送始终保留人工审批。
