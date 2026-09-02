# MCUForge 接手总手册

这份文档写给第一次接触本项目的人。不要一上来改代码；先按本文完成环境核对、启动和验收。只要能分清“Git 仓库”“HiClaw 运行环境”“项目房间”“Windows 固件工程”四件事，后续维护并不难。

## 0. 先记住四句话

1. **Git 仓库是事实源**：长期代码、角色规则、Skill、MCP 和文档都在这里。
2. **Docker 是运行环境**：容器、Matrix 房间和 MinIO 对象不是源码，不要把它们当 Git 文件。
3. **项目房间是交流事实源**：一个项目一个房间，需求、确认、派工、进度、异常和验收都留在同一处。
4. **证据比 Agent 自述更可信**：必须看到实际 diff、哈希、构建日志或硬件测试记录，不能只看“已完成”三个字。

## 1. 建议阅读顺序

第一次接手按以下顺序：

1. 本文：知道项目背景和第一次该做什么。
2. [比赛要求与作品映射](COMPETITION.md)：知道为什么这样设计。
3. [系统架构](ARCHITECTURE.md)：知道组件之间怎么连接。
4. [运行与排障手册](OPERATIONS.md)：在自己电脑上跑起来。
5. [二次开发指南](DEVELOPMENT.md)：开始修改功能。
6. [当前验证证据](VALIDATION.md)：确认哪些结论是真实跑过的。

遇到脚本参数或 HiClaw 细节，再查 [agent_infra/hiclaw/README.md](../agent_infra/hiclaw/README.md)，不必第一次全部背下来。

## 2. 比赛是什么

MCUForge 参加世界人工智能开源大赛“Agent Infra 新智基座”赛道。该赛道面向企业级复杂任务，要求作品以 HiClaw/AgentTeams 为设计基础，至少包含 3 个不同职能 Agent，并展示完整端到端闭环。

评委看的不是“聊天窗口里有几个机器人头像”，而是：

- 任务能否被稳定拆解；
- 上下文能否正确传给下一个角色；
- Agent 是否真的调用 Skill 和工具；
- 结果是否被独立验证；
- 异常能否被发现、恢复并留下证据；
- 权限、审批、回滚和审计是否清楚；
- 方案是否能复用到其他企业项目。

本项目用单片机研发作为行业场景。VCW Demo 只是一个验证实例，真正参赛产品是可复用的 MCUForge Agent Infra。

## 3. 项目是干什么的

MCUForge 让多 Agent 像一个小型嵌入式研发团队一样工作：

1. 用户用自然语言说需求。
2. Lead 把自然语言整理成工程草案，而不是马上写代码。
3. 用户确认后，Requirement 冻结验收合同。
4. Research 查官方手册、例程和许可证，沉淀事实卡片。
5. Firmware 读取真实工程，按模块化方式生成受控补丁提案。
6. Bridge 检查路径、基线和哈希，再把批准的补丁应用到实际工作树和暂存区。
7. Verification 独立查看实际 diff、固定测试和 Keil 构建证据。
8. Lead 汇总结果；烧录、COM、提交和推送仍按安全边界处理。

这样做主要解决 AI 辅助 MCU 开发中的需求误解、越权改码、无效测试、缺少复测、工程结构差、手册反复阅读、过程不透明和“假成功”等问题。

## 4. 当前 Demo 展示什么

内置 `vcw-board-demo` 使用开发板、TFT、USB CDC 和电脑端虚拟手柄，不需要车、遥控器或电机。它模拟“已有工程开发到一半，用户临时提出安全需求”的真实接手场景。

核心演示需求是：电脑控制帧中断后，开发板在规定时间内清零虚拟输出并进入安全状态；急停需要锁存；恢复必须经过连续中位帧确认。Agent 需要完成需求冻结、资料核对、补丁、构建和证据闭环，而不是只展示最终屏幕。

## 5. 你接手后的第一天怎么做

### 第一步：获取仓库

```powershell
git clone https://github.com/Asuka-WILLE/MCUForge.git
Set-Location .\MCUForge
git status --short
```

首次克隆后 `git status --short` 应没有输出。如果有输出，先不要继续，确认自己是否误改了文件。

### 第二步：确认本机前置环境

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Check-MCUForgeEnvironment.ps1
```

必需项包括 PowerShell 7、Git、Docker、Node.js 20+、npm、HiClaw Controller 和本人的 `hiclaw-manager.env`。Keil 只在真实编译固件时必需，所以环境检查把它列为可选项。

### 第三步：安装锁定依赖

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Install-MCUForge.ps1
```

它只对两个 MCP Bridge 执行 `npm ci` 和 `npm run build`。依赖版本由 `package-lock.json` 锁定；不要把 `node_modules` 提交到 Git。

### 第四步：一键启动

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Start-MCUForge.ps1
```

它会：

1. 检查并启动 Docker Desktop；
2. 恢复现有 HiClaw 容器；
3. 启动 STM32 Bridge 和 Research Bridge；
4. 注册 MCP；
5. 同步五个角色协议和工程 Skills；
6. 启动 Worker Control 可靠性服务；
7. 验证 Team 为 Active；
8. 打开 Element Web。

日志保存在 `%LOCALAPPDATA%\MCUForge\hiclaw-startup.log`。

### 第五步：验收运行态

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Test-MCUForge.ps1 -Mode Live
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Test-MCUForge.ps1 -Mode Full -CoordinationRounds 1
```

先跑 `Live`，再跑 `Full`。`Live` 只读检查 Bridge 和 Team；`Full` 会真实发送一次 Manager/Worker 协调测试消息。

### 第六步：只做一个小任务

不要第一天就换芯片或重构架构。先在 `Manager: default` 中发送 [示例新建项目请求](../examples/requests/create-project.txt)，确认 Manager 能自动建立项目房间。进入房间后再发送 [示例变更请求](../examples/requests/change-request.txt)，观察是否先出现 `INTAKE_DRAFT`。

在未回复“可以了，开始执行”前，Agent 不应派工或修改代码。这一条是最重要的安全验收。

## 6. 四种“位置”分别在哪里

### 6.1 Git 仓库

Windows 路径由你自行选择，例如：

```text
C:\Work\MCUForge
```

这里放长期维护的源码、配置模板、文档和 Demo。GitHub 只接收这里已提交的内容。

### 6.2 HiClaw 容器运行时

Manager、Lead 和四个 Worker 运行在 Docker 容器中。容器内的工作区、临时日志和进程状态都可能在重建时变化，不能当唯一事实源。

### 6.3 MinIO 共享状态

动态项目的结构化状态位于：

```text
容器工作区：/root/hiclaw-fs/shared/projects/<project-id>/
MinIO 对象：hiclaw/hiclaw-storage/shared/projects/<project-id>/
```

其中常见文件：

- `meta.json`：项目 ID、房间、状态和成员；
- `request.md`：原始需求证据；
- `plan.md`：计划与阶段；
- `audit.ndjson`：只追加的状态轨迹；
- `references/`：该项目专属参考资料；
- `tasks/<task-id>/`：任务合同、结果和证据。

不要只改容器内副本。长期资料应同时有 Git 或外部文档来源；动态状态由 filesync/MinIO 保持同步。

### 6.4 Windows 固件工程

STM32 Bridge 启动时绑定两个绝对路径：

- `ProjectRoot`：真实固件工程；
- `ProfileRoot`：合同、角色、来源、测试哈希和补丁策略。

Worker 是 Linux 容器，它看不到 `C:\...`，只能通过 STM32 MCP 读取和验证工程。若 Worker 说“没有 `/mnt/c` 所以仓库不存在”，这是错误判断；应检查 MCP，而不是复制一份源码进容器。

## 7. 日常使用流程

### 新建项目

在 `Manager: default` 发送自然语言：

```text
请创建一个新项目，名称为“某某项目”。目标是……；先澄清需求，不要立即改代码。
```

Manager 必须在当前 turn 创建独立项目房间，邀请管理员、Manager 和五个角色。房间状态达到 `READY` 前不得派工。

### 提需求

进入项目房间后说明：

- 目标；
- 当前现象；
- 希望的行为；
- 明确不能改的部分；
- 是否允许本地自动应用；
- 是否禁止烧录、COM、提交或推送。

不必自己写 Agent 专用格式，Lead 会整理成 `INTAKE_DRAFT`。

### 修改草案

直接说“第 2 条改成……”“增加一个验收条件……”即可。只要仍有修改意见，Lead 都必须更新草案并重新等待确认。

### 开始执行

确认内容无误后回复：

```text
可以了，开始执行。
```

若还希望本次自动完成本地补丁应用、固定测试和构建：

```text
确认执行，AUTO_LOCAL。
```

`AUTO_LOCAL` 不包含烧录、COM、Git commit 或 push。

### 看进度

每条机器状态应包含：

```text
[PROGRESS] run_id=... stage=... state=...
done=... current=... next=... evidence=...
```

紧随其后的中文摘要才是给人看的解释。`WAITING` 不一定是卡死；但 Worker 30 秒无回执会触发可靠性处理，Manager 不应无限等待。

### 结束项目

先让 Lead 写最终结论，确认合同、diff、构建、测试、未验证项和回滚点齐全。测试房间或废弃房间可使用 `Stop-MCUForgeProjectRooms.ps1` 终止，但该脚本会改变 Matrix 成员关系，必须先用默认 dry-run 核对精确项目 ID，再显式 `-Execute`。

## 8. 不要做的事

- 不要提交 `hiclaw-manager.env`、API Key、Matrix token、Cookie 或个人密码。
- 不要把 `node_modules`、Keil 构建目录、PC 日志和 Docker 数据打包进 Git。
- 不要通过删除测试、改测试哈希或屏蔽错误来制造通过。
- 不要让 Research 把博客当作唯一芯片事实；优先官方手册和官方仓库。
- 不要让 Verification 顺手改实现；它必须保持独立。
- 不要对整个仓库执行 reset、clean 或批量删除；先看 `git status` 并保护用户改动。
- 不要把容器 `Running` 当成消息链路正常；必须做真实协调验收。

## 9. 接手完成标准

当你能独立完成以下事项，才算真正接手：

- 从干净克隆完成环境检查、安装、启动和 Live/Full 验收；
- 解释五个 Agent、Manager、Matrix、MinIO、三个 Bridge 的职责；
- 创建一个项目房间，并说出 `READY` 的硬条件；
- 让 Lead 生成草案但不提前派工；
- 找到某次任务的合同、补丁、构建日志和哈希；
- 知道 `AUTO_LOCAL` 能做什么、不能做什么；
- 能在 Worker 在线但不回复时检查策略守护、即时回执和恢复链路；
- 能按 [二次开发指南](DEVELOPMENT.md) 新建自己的 Demo/Profile，而不是覆盖 VCW 示例；
- 能执行测试、审阅 diff、按 Asuka 身份提交并推送 GitHub。

如果其中任何一项做不到，先查对应文档和日志，不要靠猜测修改运行环境。
