# MUC_AGENT HiClaw 使用说明

这份说明面向第一次拿到本仓库的人：如何在自己的 Windows 电脑上启动 MUC_AGENT 的 HiClaw 团队，并用它完成一次**可审计的 UM10550 开发板 Demo 开发任务**。

它不是把“让 AI 随便改代码”包装成多 Agent。当前流程是：人提出需求 → Lead 拆解 → Requirement 冻结验收 → Research 查公开资料 → Firmware 交付受限补丁 → Verification 独立验证 → 人批准后才应用补丁、烧录或推送。

## 1. 你会得到什么

启动后会创建 `mcuforge` Team，包含五个角色：

| 角色 | 名称 | 负责什么 |
| --- | --- | --- |
| Lead | `mcuforge-lead` | 接收人的需求、拆解任务、组织交接、汇总结论。 |
| Requirement | `mcuforge-requirements` | 把自然语言需求冻结为验收合同与边界。 |
| Research | `mcuforge-research` | 检索允许来源内的公开手册、例程和许可证信息。 |
| Firmware | `mcuforge-firmware` | 读取工程、分析实现并交付受限的统一 diff。 |
| Verification | `mcuforge-verification` | 校验固定测试、运行构建、报告证据与缺口。 |

当前默认模型是 `deepseek-v4-pro`，可通过 `Bootstrap-MCUForgeTeam.ps1 -Model <模型名>` 覆盖。模型 API Key 只属于每位使用者自己的 HiClaw 配置，**不要复制、提交或发送任何人的 `hiclaw-manager.env`、令牌或密钥文件**。

## 2. 当前可复现范围

本仓库开箱即用的实例是 [UM10550 开发板安全控制 Demo](../../demos/um10550-board-demo/README.md)：STM32H723、TFT、USB CDC、电脑虚拟手柄；不需要车辆、遥控器或电机。

冻结任务为 `MCUFORGE-FS-002`，用于演示电脑控制断链、急停和受控恢复。默认补丁白名单只允许修改：

```text
demos/um10550-board-demo/firmware/Core/Src/mcuforge_demo.c
demos/um10550-board-demo/firmware/Core/Inc/mcuforge_demo.h
```

这是刻意的安全设计。超出这两个文件、烧录、打开 COM 口和 Git 推送，均必须由人明确批准。

## 3. 前置条件

在开始前准备好：

1. Windows 10/11、PowerShell 7（命令为 `pwsh`）和 Git。
2. Docker Desktop 已启动；HiClaw 已按[官方安装说明](https://github.com/agentscope-ai/HiClaw)完成本机安装，并配置自己的模型供应商和 API Key。
3. Node.js 20 或更高版本，用于两个本机 MCP 桥。
4. 若要实际运行 Keil 构建，另需安装 Keil uVision 5；若要跑硬件固定测试，还需已烧录的开发板、USB CDC 端口和 Python/pyserial。

检查基础环境：

```powershell
pwsh -NoProfile -Command 'pwsh --version; docker version; node --version'
pwsh -NoProfile -Command 'docker inspect -f ''{{.State.Running}}'' hiclaw-controller'
```

最后一条应输出 `true`。本项目假设 HiClaw 嵌入式安装的 Controller 容器名为 `hiclaw-controller`；若你的安装使用了不同名称，请在后续脚本中传入 `-Controller <容器名>`。

## 4. 获取代码与安装 Node 依赖

```powershell
git clone https://github.com/Asuka-WILLE/MUC_AGENT.git
Set-Location .\MUC_AGENT
git status --short

Set-Location .\agent_infra\tool_bridge\stm32-mcp-server
npm ci
npm run build

Set-Location ..\research-web-mcp-server
npm ci
npm run build

Set-Location ..\..\..
```

`git status --short` 在开始时应没有输出。不要把 `node_modules`、`%LOCALAPPDATA%\MCUForge` 下的桥接令牌或自己的 HiClaw 环境文件加入 Git。

## 5. 首次启动：按顺序执行

以下命令均从仓库根目录执行。两个 Bridge 会以前台服务运行，因此请分别打开两个 PowerShell 7 窗口。

### 窗口 A：启动 STM32 工程桥

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\agent_infra\tool_bridge\stm32-mcp-server\Start-STM32ToolBridge.ps1
```

它默认绑定本仓库的 UM10550 `firmware/` 与 `agent_profile/`，只给 Firmware 和 Verification 提供：工程快照、受限文件读取、Git diff、测试完整性检查和 Keil 构建。它没有任意 Shell、源码写入、烧录或串口工具。

### 窗口 B：启动公开资料检索桥

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\agent_infra\tool_bridge\research-web-mcp-server\Start-ResearchWebBridge.ps1
```

这个桥只会提供给 Research：公开 HTTPS 技术页面的白名单检索和读取。它不允许登录、Cookie、下载、执行文件、私网访问或仓库写入。

### 窗口 C：注册 MCP、创建 Team

先确认两项健康检查都返回 `status: ok`：

```powershell
pwsh -NoProfile -Command 'Invoke-WebRequest http://127.0.0.1:8765/health | Select-Object -ExpandProperty Content'
pwsh -NoProfile -Command 'Invoke-WebRequest http://127.0.0.1:8766/health | Select-Object -ExpandProperty Content'
```

然后将两个 Bridge 注册到你自己的 HiClaw 网关。`HiClawEnvPath` 必须指向**你自己**安装时生成的环境文件，通常位于 `$env:USERPROFILE\hiclaw-manager.env`：

```powershell
$hiclawEnv = Join-Path $env:USERPROFILE 'hiclaw-manager.env'
Test-Path $hiclawEnv

pwsh -NoProfile -ExecutionPolicy Bypass -File .\agent_infra\tool_bridge\stm32-mcp-server\Configure-HiClawProxy.ps1 `
  -HiClawEnvPath $hiclawEnv

pwsh -NoProfile -ExecutionPolicy Bypass -File .\agent_infra\tool_bridge\research-web-mcp-server\Configure-HiClawResearchProxy.ps1 `
  -HiClawEnvPath $hiclawEnv
```

两个配置脚本会各自重新应用 Team；最后再执行一次幂等启动，确保五个角色、两个 MCP 和自定义 Skills 都已加载：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\agent_infra\hiclaw\Bootstrap-MCUForgeTeam.ps1 `
  -EnableToolBridge -EnableResearchBridge
```

验证 Team：

```powershell
pwsh -NoProfile -Command 'docker exec hiclaw-controller hiclaw get teams mcuforge'
pwsh -NoProfile -Command 'docker exec hiclaw-controller hiclaw get workers --team mcuforge'
```

预期：Team 为 `Active`，并有 Leader 加四个 ready Worker。

## 6. 如何向 Team 发需求

在浏览器打开 HiClaw 的 Element Web（本机默认是 `http://127.0.0.1:18088`），使用**你自己的** HiClaw 管理员账号登录。也可用 `http://127.0.0.1:18888` 打开 Manager/OpenClaw 控制界面。

进入 `mcuforge` Team 房间，或给 Leader 私聊。不要硬编码 Matrix 地址；先用上一节的 `get workers` 输出确认本机 Leader 的实际 ID，然后在消息中 @ 提及它。你只需要说自然语言，不需要自己写 Requirement/Firmware 的任务格式。

例如直接发送：

```text
我想让电脑控制帧中断后，开发板自动进入安全状态并清零虚拟输出。
现在的 Demo 会保持最后一条控制命令，我希望超过 150 ms 没有新帧时停止。
不要改 SBUS、RS485 或电机逻辑，也不要烧录、操作 COM 口或推送。
```

Leader 不会因为收到这句话就开始改代码，而是先返回 `INTAKE_DRAFT`，把目标、现状、验收、非目标、影响范围、验证计划和待确认问题整理给你。你可以继续说“把超时改成 200 ms”“还要增加恢复条件”等修改意见；Leader 会更新草案。

只有当你明确回复 `可以了，开始执行`、`确认执行` 或 `开始执行` 后，Leader 才会返回 `INTAKE_CONFIRMED` 并正式安排 Requirement、Research、Firmware 和 Verification。单独回复“好”“嗯”“可以”“继续”不会触发执行，避免误启动。

确认后的交付顺序才是：合同 → 来源与事实卡片 → 补丁提案 → 固定测试完整性 → Keil 构建证据 → 等待你的后续批准。若某个证据尚未取得，Agent 必须写明“未验证”和原因，而不能把静态检查说成硬件通过。

## 7. 审核并应用 Agent 给出的补丁

Firmware Agent 只能给出 `.patch`，不会直接写你的 Windows 工程。拿到补丁后，先登记并校验：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\agent_infra\patch_channel\New-MCUForgePatchProposal.ps1 `
  -PatchFile C:\明确路径\proposal.patch `
  -ProposalId FS-002-001
```

脚本会拒绝脏工作树、越出白名单的文件、基线不一致或被篡改的固定测试。审阅生成的 `proposal.json` 与 `proposal.patch` 后，才使用其中的 SHA-256 组成精确批准令牌：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\agent_infra\patch_channel\Apply-MCUForgeApprovedPatch.ps1 `
  -ProposalDirectory .\demos\um10550-board-demo\agent_profile\patch_proposals\FS-002-001 `
  -ApprovalToken 'APPLY FS-002-001 <proposal.json 中的 patch.sha256>'
```

这一步只会 `git apply --index`，不会自动提交、推送、烧录或打开 COM 口。之后由 Verification 跑构建；只有你再次明确批准，才能烧录并执行例如 `-Port COM3 -Case FS-001` 的硬件固定测试。

## 8. 自定义工程 Skills

`../skills/general-engineering-principles-2026-08-16/` 内含九个自定义 `.skill` 包，例如证据驱动调试、接口契约演进、回归安全、韧性状态机和分层验证。

`Bootstrap-MCUForgeTeam.ps1` 默认会安装它们。若更新了同名 `.skill` 包，单独运行：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\agent_infra\hiclaw\Install-MCUForgeSkills.ps1
```

安装器会检查每个 ZIP 是否恰好包含一个 `<名称>/SKILL.md`、有效的 YAML 元数据和 SHA-256；随后将它解包到 Worker 工作区的 `skills/<名称>/SKILL.md`，写入 HiClaw 的持久存储并同步到五个角色。它不会删除其他已有 Skill，也不会扩大 Agent 权限。OpenClaw 会从工作区的 `skills/<名称>/SKILL.md` 自动发现技能。[HiClaw/AgentTeams 开发说明](https://github.com/agentscope-ai/AgentTeams/blob/main/docs/development.md)

## 9. 停止、重启与常见问题

### 停止和重启

- 停止两个本机 Bridge：在窗口 A、B 按 `Ctrl+C`。
- 重新启动 Docker Desktop/HiClaw 后：先重启窗口 A、B 的 Bridge，再执行窗口 C 的两个配置脚本和最后的 Bootstrap 命令。
- 只想更新 Team 角色或 Skill，不重新配置 MCP：运行 `Bootstrap-MCUForgeTeam.ps1`；它会按源文件哈希同步 Leader 和 Worker 角色协议，只有协议真的变化时才重启对应容器；若要跳过同名 Skill 覆盖，加 `-SkipBundledSkills`。

### 常见问题

| 现象 | 先检查什么 |
| --- | --- |
| `HiClaw controller is not running` | Docker Desktop 是否启动；`docker ps` 中是否有 `hiclaw-controller`。 |
| `dist/index.js is missing` | 对对应 Bridge 目录执行 `npm ci` 和 `npm run build`。 |
| 配置脚本找不到环境文件 | 检查 `$hiclawEnv`，并显式传入自己的 `-HiClawEnvPath`；不要复制他人的环境文件。 |
| Worker 没有 MCP 工具 | 两个 `/health` 是否正常；重新执行对应 `Configure-HiClaw...Proxy.ps1`，再执行 Bootstrap。 |
| Agent 声称可以直接改/烧录/推送 | 这是越权结论。当前配置本来就没有开放这些工具；应要求它交付 patch 和证据。 |
| 需要换成自己的工程 | 不要直接把文件替换进 Demo。先创建新的 `demos/<名称>/firmware` 与 `agent_profile`，冻结合同、固定测试哈希、角色规则和补丁白名单；再让 STM32 Bridge 显式绑定新的 `-ProjectRoot` 与 `-ProfileRoot`。 |

## 10. 给新工程使用者的边界

本仓库目前已经把运行时与 Demo 分开，但**只为 UM10550 Demo 提供了完整冻结 Profile 和已验证 MCP 配置**。新工程复用时，以下内容必须由项目负责人重新建立，不能沿用 Demo 的结论：

1. 项目事实源、构建入口、芯片与工具链。
2. 需求合同、允许修改范围、不可变测试与证据标准。
3. Research 需要的芯片手册、库版本、许可证结论。
4. 新工程的 `ProjectRoot`、`ProfileRoot` 和 MCP 授权边界。

资料、验收或安全边界缺失时，正确行为是让 Agent `blocked` 并向人索要信息；不要为了“跑通演示”让它编造手册、扩大工具权限或绕过测试。
