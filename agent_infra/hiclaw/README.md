# MCUForge HiClaw Team

本目录保存 MCUForge 在 HiClaw（AgentTeams 原名）中的可复现团队配置。它面向两类任务：接手已有 MCU 工程，以及从器件和需求出发建立新工程。

## 团队

| Worker | 身份 | 主要交付物 |
| --- | --- | --- |
| `mcuforge-lead` | Team Leader | 任务状态、拆解、交接和最终结论；使用 HiClaw 内置 Team Leader 编排模板 |
| `mcuforge-requirements` | Requirement & Architecture | 冻结的验收合同 |
| `mcuforge-research` | Research & Knowledge | 来源清单、事实卡片、许可证结论 |
| `mcuforge-firmware` | Firmware Engineer | 受限源码变更和构建摘要 |
| `mcuforge-verification` | Verification & Evidence | 独立验证和证据报告 |

核心角色默认使用 `deepseek-v4-pro`。当前 HiClaw 版本由 Team 资源管理 Leader，普通 Worker 接口不能覆盖其 SOUL；项目专属 Leader 规则保存在 `roles/leader/SOUL.md`，并在任务启动时作为团队协议传入。其余四个 Worker 直接使用各自的专属 SOUL。Worker 只通过结构化交接物协作，不能用聊天结论代替合同、来源、构建或测试证据。

## 创建或更新

从仓库根目录执行：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File agent_infra\hiclaw\Bootstrap-MCUForgeTeam.ps1
```

脚本是幂等的：已存在的 Team/Worker 会被更新，不会重复创建。它不会烧录、推送或删除任何仓库文件。

## 发布冻结任务上下文

任务开始前，先把已提交的工程事实、验收合同和来源清单发布到 Team 共享存储：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File agent_infra\hiclaw\Publish-MCUForgeSharedContext.ps1
```

默认发布 `MCUFORGE-FS-001`。脚本拒绝从脏工作树发布，记录内容哈希，并拒绝用相同 `run_id` 覆盖不同上下文；要修改合同，必须创建新的 run。四个 OpenClaw Worker 使用 `/root/hiclaw-fs/shared/mcuforge/runs/<run_id>/`，Leader 也同步至同一路径。

## 当前接入状态

已完成：

1. `mcuforge` Team 与 Leader、Requirement、Research、Firmware、Verification 五个角色运行；
2. Windows STM32 Tool Bridge 通过 Higress MCP Proxy 接入 Firmware/Verification；
3. Worker 已端到端调用工程快照、固定测试哈希审计和真实 Keil 全量构建；
4. 所有 Windows 脚本统一使用 PowerShell 7（`pwsh`）；
5. 烧录、COM 口、源码写入和远程推送仍未开放。

下一阶段：把工程地图、任务合同和来源清单同步到 Team 共享目录，给 Research Agent 接入受控互联网检索，再设计只能修改白名单文件且可审计回滚的补丁通道。烧录和远程推送始终保留人工审批。
