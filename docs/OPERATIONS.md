# MCUForge 运行与排障手册

本文所有命令都在 Windows PowerShell 7（`pwsh`）中执行。除非命令明确写了其他目录，否则工作目录都是仓库根目录。

## 1. 前置环境

### 必需

- Windows 10/11；
- PowerShell 7；
- Git for Windows；
- Docker Desktop；
- HiClaw/AgentTeams 本地部署；
- Node.js 20 或更高版本；
- 可用的模型供应商配置和个人 API Key。

### 按功能选装

- Keil uVision 5：真实编译 VCW 固件；
- Python 3 和 `pyserial`：运行 PC 工具或硬件串口测试；
- 开发板、USB 线、TFT：真实硬件演示。

仓库不包含模型 Key、HiClaw 账号密码或可公开共享的比赛账号。每位使用者必须配置自己的环境。

## 2. 从零安装

### 2.1 克隆

```powershell
git clone https://github.com/Asuka-WILLE/MCUForge.git
Set-Location .\MCUForge
git status --short
```

预期：最后一条命令无输出。

### 2.2 准备本地配置记录

```powershell
Copy-Item .\config\mcuforge.local.example.psd1 .\config\mcuforge.local.psd1
```

用编辑器把 `HiClawEnvPath` 改成自己的实际路径。`mcuforge.local.psd1` 已被 `.gitignore` 排除，仅作为本人记录；当前启动脚本仍通过命令行参数接收这些值。例如：

```powershell
$settings = Import-PowerShellDataFile .\config\mcuforge.local.psd1
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Check-MCUForgeEnvironment.ps1 `
  -HiClawEnvPath $settings.HiClawEnvPath `
  -Controller $settings.Controller
```

不要把真实 API Key 写入此文件。模型 Key 只放在 HiClaw 自己的安全配置中。

### 2.3 环境检查

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Check-MCUForgeEnvironment.ps1
```

检查器不会安装或修改任何东西。它会把 Bridge 和 Keil 标为可选运行态，因此首次执行时两项 Bridge 显示失败是正常的；所有“必需=是”的项目必须通过。

如果 `hiclaw-manager.env` 不在默认位置：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Check-MCUForgeEnvironment.ps1 `
  -HiClawEnvPath 'D:\HiClaw\hiclaw-manager.env'
```

### 2.4 安装依赖

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Install-MCUForge.ps1
```

脚本会在两个 Bridge 目录执行锁定安装与 TypeScript 构建。首次执行需要访问 npm registry。成功后应存在：

```text
agent_infra/tool_bridge/stm32-mcp-server/dist/index.js
agent_infra/tool_bridge/research-web-mcp-server/dist/index.js
```

`dist` 和 `node_modules` 是可再生物，不提交 Git。

### 2.5 准备 MCUForge Worker 镜像（首次必做）

mcuforge Team 的 Worker 依赖两个本地镜像：`local/mcuforge-hiclaw-worker:policy-safe-20260902-v2`
（HiClaw 官方 `hiclaw-worker` 之上打的策略安全层）与 `local/mcuforge-worker-control:20260902`。
二者构建源都在仓库内，联网可自动构建；也可用交付包离线导入。

**联网构建（推荐）**：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File `
  .\agent_infra\hiclaw\worker-image-policy-safe\Build-MCUForgeWorkerImage.ps1
```

首次会拉取官方基础镜像（约 2-4 GB）与 `python:3.12-alpine`。若你的 HiClaw
来自其它区域 registry，用 `-BaseImage <你的 hiclaw-worker 镜像>` 覆盖。

**离线导入**（若随交付包提供 tar）：

```powershell
docker load -i .\mcuforge-hiclaw-images.tar.gz
```

完成后重跑 `Check-MCUForgeEnvironment.ps1`：两项"MCUForge … 镜像"检查应显示通过，
若 2.3 阶段曾因缺镜像报失败，此时应已消除。

## 3. 启动

### 3.1 推荐：根目录一键启动

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Start-MCUForge.ps1
```

如果环境文件不在默认位置：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Start-MCUForge.ps1 `
  -HiClawEnvPath 'D:\HiClaw\hiclaw-manager.env'
```

常用参数：

| 参数 | 作用 |
| --- | --- |
| `-SkipDependencyBuild` | 已有 `dist/index.js` 时跳过依赖构建 |
| `-ForceTeamBootstrap` | Team 看起来 Ready 也强制重新同步协议和 Skill |
| `-NoBrowser` | 不自动打开 Element Web |
| `-Controller <名称>` | HiClaw Controller 容器不是默认名称时使用 |

启动日志：

```text
%LOCALAPPDATA%\MCUForge\hiclaw-startup.log
```

本机默认地址：

| 服务 | 地址 |
| --- | --- |
| Element Web | `http://127.0.0.1:18088` |
| Manager/OpenClaw | `http://127.0.0.1:18888` |
| STM32 Bridge 健康端点 | `http://127.0.0.1:8765/health` |
| Research Bridge 健康端点 | `http://127.0.0.1:8766/health` |

### 3.2 手动启动（用于定位问题）

打开三个 PowerShell 7 窗口。

窗口 A：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File `
  .\agent_infra\tool_bridge\stm32-mcp-server\Start-STM32ToolBridge.ps1
```

窗口 B：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File `
  .\agent_infra\tool_bridge\research-web-mcp-server\Start-ResearchWebBridge.ps1
```

窗口 C：

> **首次部署顺序**：必须先执行 Bootstrap-MCUForgeTeam.ps1 让 mcuforge Team/Worker 注册，
> HiClaw 网关才会为 5 个 Worker 生成 key-auth consumer；两个 Configure 脚本会校验这些
> consumer 是否已存在。若在全新环境（网关无 consumer）下，Configure 脚本也会自动执行
> 一次 Bootstrap 并等待 consumer 生成（默认最多 120 秒），因此直接按下面顺序执行即可。

```powershell
$hiclawEnv = Join-Path $env:USERPROFILE 'hiclaw-manager.env'

pwsh -NoProfile -ExecutionPolicy Bypass -File `
  .\agent_infra\hiclaw\Bootstrap-MCUForgeTeam.ps1 `
  -EnableToolBridge -EnableResearchBridge -EnableWideAgentAccess

pwsh -NoProfile -ExecutionPolicy Bypass -File `
  .\agent_infra\tool_bridge\stm32-mcp-server\Configure-HiClawProxy.ps1 `
  -HiClawEnvPath $hiclawEnv -EnableWideAgentAccess

pwsh -NoProfile -ExecutionPolicy Bypass -File `
  .\agent_infra\tool_bridge\research-web-mcp-server\Configure-HiClawResearchProxy.ps1 `
  -HiClawEnvPath $hiclawEnv -EnableWideAgentAccess
```

## 4. 验收

### 4.1 Static：不依赖正在运行的 HiClaw

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Test-MCUForge.ps1 -Mode Static
```

覆盖 PowerShell 解析、Python AST、可用时的 Bash 语法、两个 TypeScript 构建和 `git diff --check`。

### 4.2 Live：只读运行态

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Test-MCUForge.ps1 -Mode Live
```

额外检查两个 Bridge 健康端点和 HiClaw Team：`Active`、Leader ready、4/4 Workers。

### 4.3 Full：真实消息链路

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Test-MCUForge.ps1 `
  -Mode Full -CoordinationRounds 1
```

这会真实向 Matrix 发送协调验收消息，检查 Manager 和四个 Worker 的回执时延。它不改固件、不烧录、不操作 COM。

### 4.4 项目房间专项验收

已有项目：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File `
  .\agent_infra\hiclaw\Test-MCUForgeProjectRoom.ps1 `
  -ProjectId '<project-id>'
```

自然语言建房完整回归会创建真实测试房间，仅在修改建房机制后执行：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File `
  .\agent_infra\hiclaw\Test-MCUForgeNaturalLanguageProject.ps1 `
  -Title 'MCUForge 自然语言建房验收'
```

测试结束后按精确 `project_id` dry-run，再终止测试房间；不要批量处理不认识的房间。

## 5. 日常健康检查

```powershell
Invoke-RestMethod http://127.0.0.1:8765/health
Invoke-RestMethod http://127.0.0.1:8766/health

docker exec hiclaw-controller hiclaw get teams mcuforge
docker exec hiclaw-controller hiclaw get workers --team mcuforge

docker exec hiclaw-manager curl -fsS `
  http://mcuforge-worker-control:18765/health
```

Worker Control 的健康结果至少关注：

- `manager_policy.ok`：Manager 允许接收 Worker 消息；
- `worker_policy.ok`：Worker 允许接收 Manager 消息；
- `coordinator_relay.ok`：即时回执监听器正常。

容器状态是 Running 但上述任一项失败，都不能判定协作链路健康。

## 6. 常见故障

### 6.1 `hiclaw-manager.env` 找不到

原因：使用了其他人的路径或安装没有完成。

处理：找到本人部署生成的文件，然后显式传 `-HiClawEnvPath`。不要复制他人的环境文件，因为其中可能有密钥和本地端口。

### 6.2 Bridge 端口无响应

```powershell
Get-NetTCPConnection -LocalPort 8765,8766 -ErrorAction SilentlyContinue
Get-Content (Join-Path $env:LOCALAPPDATA 'MCUForge\hiclaw-startup.log') -Tail 100
```

若 `dist/index.js` 缺失，重新执行 `Install-MCUForge.ps1`。若端口被其他进程占用，先确认进程身份；不要盲目结束系统进程。

### 6.3 Team 不是 Active 或不是 4/4

```powershell
docker exec hiclaw-controller hiclaw get teams mcuforge -o json
docker exec hiclaw-controller hiclaw get workers --team mcuforge
```

确认模型服务没有 503、API Key 有效、Docker 磁盘未满，然后执行：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Start-MCUForge.ps1 `
  -ForceTeamBootstrap
```

### 6.4 Worker 在线但不回复

先跑一次真实协调测试。若失败，再读 Worker Control 健康端点。系统策略是两次 30 秒无回执后自动 restart，仍失败再 recreate；不要在聊天里无限“再等等”。

如果要人工检查精确 Worker：

```powershell
docker exec hiclaw-manager curl -fsS `
  http://mcuforge-worker-control:18765/v1/workers/mcuforge-research
```

人工调用 restart/recreate 会改变容器状态，只在已确认任务链路失效时使用，并保留输出记录。

### 6.5 Manager 要等心跳才回复

检查 `coordinator_relay.ok`，以及 Worker 消息是否同时满足完整 Matrix mention、真实 `m.mentions.user_ids` 和非 `m.replace`。重新 Bootstrap 会恢复策略；不要把心跳间隔改成几秒来掩盖事件链路错误。

### 6.6 Worker 说找不到 Windows 仓库

Worker 不应该访问 `C:\` 或 `/mnt/c`。让它调用 `stm32_get_project_snapshot`。若工具不存在，重新注册 STM32 Proxy 和 Bootstrap；不要复制一份工程进 Worker 容器。

### 6.7 补丁工具返回成功但 diff 为空

先分别检查工作树和暂存区 diff。当前正确桥实现会把批准补丁同时物化到工作树和 Git index，并在返回成功前验证。Verification 应调用 `stm32_get_git_diff(cached=true)`；若仍为空，停止构建并保留 proposal/apply 证据，不要循环应用。

### 6.8 HEAD 漂移或基线过期

权威基线是 Windows `agent_profile/patch-policy.json`。非固件历史漂移只有在允许路径源码哈希完全一致时才能自动继续；固件源码已变时必须重新冻结一次，不能手改共享 Markdown 冒充新基线。

### 6.9 模型服务 503

这不是 Firmware 代码错误。先检查 HiClaw 的 provider/model 配置和服务额度，确认预检成功再恢复任务。不要在同一失效模型上无限重试；恢复后用原 `project_id` 和 `task_id` 继续，以保留轨迹。

### 6.10 Docker 占用异常增长

先只读检查：

```powershell
docker system df -v
docker ps --size
```

不要直接执行全局 prune。区分镜像、构建缓存、容器可写层、volume 和项目源文件后，再对明确对象处理；Matrix/MinIO volume 可能包含项目证据。

## 7. 更新代码后的标准流程

```powershell
git status --short
git diff --check
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Test-MCUForge.ps1 -Mode Static
```

若修改角色、项目房间 Skill 或 Worker Control：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Start-MCUForge.ps1 `
  -ForceTeamBootstrap
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Test-MCUForge.ps1 `
  -Mode Full -CoordinationRounds 3
```

修改项目房间机制还需额外运行自然语言建房回归，并终止生成的测试房间。

## 8. 生成比赛代码包

先确保受跟踪文件已提交：

```powershell
git status --short
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Build-MCUForgePackage.ps1
```

输出位于 `artifacts/`，包括 ZIP 和 `.sha256`。ZIP 来自 `git archive HEAD`，所以不会包含本地密钥、未跟踪文件、运行缓存或脏工作树改动。

## 9. 最小备份策略

- 长期代码：推送 GitHub；
- 比赛材料：另存 PPT/PDF/视频，不提交超大视频到源码仓库；
- 动态项目证据：按项目导出 MinIO 对象和 Matrix 轨迹；
- 本地密钥：使用安全密码管理或重新生成，不上传 Git；
- 固件构建产物：记录 SHA 和构建日志，需要时作为发布附件，不把全部 Keil 缓存提交。
