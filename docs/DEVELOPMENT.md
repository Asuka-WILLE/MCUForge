# MCUForge 二次开发指南

本指南说明如何安全地修改 MCUForge、接入新单片机工程，并把改动交给下一位开发者。开始前先完成 [运行与排障手册](OPERATIONS.md) 的 Static、Live 和 Full 验收。

## 1. 开发前保护基线

```powershell
git status --short
git branch --show-current
git remote -v
git log -5 --oneline
```

如果工作树已有修改，先确认是谁改的、是否与本任务相关。不要使用 `git reset --hard`、`git clean -fd` 或覆盖目录来追求“干净”。

建议从 GitHub 最新分支创建功能分支：

```powershell
git switch -c feature/<简短功能名>
```

## 2. 改哪个目录

| 需求 | 修改位置 |
| --- | --- |
| Manager/Worker 通信、建房、恢复 | `agent_infra/hiclaw/` |
| 读工程、Git、Keil、补丁策略 | `agent_infra/tool_bridge/stm32-mcp-server/` |
| 公开资料检索 | `agent_infra/tool_bridge/research-web-mcp-server/` |
| 通用工程方法 | `agent_infra/skills/` |
| 本地补丁登记/应用 | `agent_infra/patch_channel/` |
| 某个 Demo 的固件和测试 | `demos/<demo>/firmware/` |
| 某个 Demo 的角色、合同和白名单 | `demos/<demo>/agent_profile/` |
| 新人说明、架构和运维 | `docs/` 与根 README |

不要把某块板的测试或源码放进 `agent_infra`。Infra 只能依赖显式传入的 `ProjectRoot` 和 `ProfileRoot`。

## 3. 接入一个新 MCU 工程

### 3.1 建立目录

推荐结构：

```text
demos/<project-slug>/
├── README.md
├── firmware/                    # 原始可构建工程
└── agent_profile/
    ├── patch-policy.json        # 路径与基线门禁
    ├── testcase-lock.json       # 固定测试哈希
    ├── hiclaw_roles/            # 项目角色补充规则
    ├── shared_context/          # 合同、项目事实、来源和 manifest
    ├── tasks/                   # 可复现任务输入/结果
    └── patch_proposals/         # 运行生成物，不提交
```

先导入能独立构建的原始工程，再建立 Profile。不要一边迁移一边让 Agent 改功能，否则无法证明基线。

### 3.2 冻结事实源

至少记录：

- 芯片、封装和板卡版本；
- CubeMX/SDK/HAL/RTOS 版本；
- Keil 工程文件和编译器版本；
- 关键外设、引脚、时钟和生成代码边界；
- 当前 Git HEAD 和允许路径源码 SHA-256；
- 已知问题和明确非目标；
- 原作者和第三方许可证。

### 3.3 冻结补丁白名单

`patch-policy.json` 应只允许本需求真正需要的文件。首次新工程不要直接允许整个 `Core/`。路径越小，越容易审计和回滚。

Profile 中的 Markdown 是给人看的镜像，JSON 才是桥接器门禁。任何自动化都不得用聊天记录覆盖 JSON 基线。

### 3.4 固定测试

把测试与实现分开，记录测试文件哈希。固定测试至少覆盖：

- 正常路径；
- 边界值；
- 超时/断链；
- 错误输入；
- 恢复条件；
- 原有行为不变量。

Agent 不得修改测试或哈希来获得通过。

### 3.5 启动自定义工程 Bridge

先停止现有 STM32 Bridge 窗口，然后显式绑定新路径：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File `
  .\agent_infra\tool_bridge\stm32-mcp-server\Start-STM32ToolBridge.ps1 `
  -ProjectRoot 'C:\Work\MCUForge\demos\my-board\firmware' `
  -ProfileRoot 'C:\Work\MCUForge\demos\my-board\agent_profile'
```

当前启动器会检查 `MDK-ARM\VCW.uvprojx`，这是 VCW 示例的固定约束。要支持另一种 Keil 工程名，必须先把该检查演进为 Profile 声明的构建入口，并同步修改 MCP 的构建工具和测试；不要为了跑通直接删除检查。

## 4. 修改 Agent 角色

角色规则位于：

```text
demos/vcw-board-demo/agent_profile/hiclaw_roles/<role>/SOUL.md
```

每个角色至少说明：

- 身份与职责；
- 输入和输出；
- 共享上下文读取方式；
- 可用工具与依赖；
- 决策边界；
- 进度和 Trace 格式；
- 失败与升级条件；
- 明确禁止事项。

修改后执行 Bootstrap。脚本按内容哈希同步协议，只重启真正变化的角色。然后至少运行三轮 Full 协调验收。

## 5. 新增或修改 Skill

一个 Skill 应回答：

- 什么情况触发；
- 输入是什么；
- 输出和证据是什么；
- 依赖哪些工具；
- 权限边界是什么；
- 失败时如何停止或回滚；
- 如何跨项目复用；
- 版本如何升级和回退。

通用 Skill 放 `agent_infra/skills/<skill-name>/`；项目专属步骤放 `agent_profile`，不要把某个板卡的绝对路径写进通用 Skill。

更新 `.skill` 包后：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File `
  .\agent_infra\hiclaw\Install-MCUForgeSkills.ps1
```

安装器会校验包结构和 SHA，再同步到 Leader/Worker 与 MinIO。

## 6. 新增 MCP 工具

新增工具时先写工具合同：

1. 精确输入 schema；
2. 正常输出和错误输出；
3. 可访问的资源范围；
4. 超时、大小和速率限制；
5. 幂等性；
6. 审批条件；
7. 审计字段；
8. 回滚或停止条件。

原则：宁可增加一个窄工具，也不要暴露任意 Shell。例如“运行固定 Keil 工程构建”优于“执行任意命令”。

修改 TypeScript 后：

```powershell
Set-Location .\agent_infra\tool_bridge\stm32-mcp-server
npm run build
npm run test:client
Set-Location ..\..\..
```

同步更新对应 README、Configure Proxy 的权限列表、角色依赖和验证证据。

## 7. 修改项目房间机制

关键文件：

- `manager-project-room/SKILL.md`：触发与状态机；
- `manager-project-room/AGENTS.policy.md`：Manager 强制策略；
- `manager-project-room/scripts/create-project.sh`：真实建房和 MinIO 同步；
- `Install-MCUForgeProjectRoomPolicy.ps1`：安装到 Manager 工作区、执行路径和 MinIO；
- `Test-MCUForgeProjectRoom.ps1`：项目契约验收；
- `Test-MCUForgeNaturalLanguageProject.ps1`：真实自然语言入口回归。

修改后必须验证：

- 同一请求不会创建重复房间；
- 7/7 成员；
- 原始请求正文与 SHA；
- `meta.json` schema v2；
- `project_room_only`；
- `[PROJECT_CREATED]` 和 `[ORIGINAL_REQUEST]`；
- MinIO 和 `audit.ndjson`；
- `BLOCKED` 后同 ID 恢复；
- `TERMINATED` 后不会继续派工。

测试房间必须按明确 `project_id` 终止，不能清空所有 Matrix 房间。

## 8. 修改可靠性机制

Worker Control Bridge 的高权限来自 Docker socket，因此修改时重点检查：

- Worker 名称正则仍是严格白名单；
- API 没有接受任意容器名或任意命令；
- restart/recreate 有次数上限；
- 初始 Matrix sync 不重放旧消息；
- 临时异常保留上次成功 cursor；
- ACK 不冒充最终业务结果；
- 策略同时写本地配置和 MinIO；
- `/health` 能暴露 manager、worker 和 relay 三类状态。

修改后至少连续运行 3 轮协调测试。可控地停止一个测试 Worker 后，观察一次 restart/recreate 恢复；不得在正在处理真实项目时做故障注入。

## 9. 验证矩阵

| 变更类型 | 最低验证 |
| --- | --- |
| 只改 Markdown | `git diff --check` + 链接/事实核对 |
| PowerShell | Parser + 实际相关脚本 dry-run |
| Python Bridge | AST + 容器构建 + `/health` |
| TypeScript MCP | `npm run build` + `test:client` + 实际工具调用 |
| 角色/策略 | Bootstrap + Full 协调 3 轮 |
| 项目房间 | 专项验收 + 自然语言回归 + 终止测试房间 |
| 固件 | 固定测试哈希 + Keil 全量构建 + HEX/AXF SHA |
| 硬件行为 | 以上全部 + 明确批准后的真实板卡测试 |

任何报告都要把“执行了哪些层”和“没执行哪些层”分开写。

## 10. Git 提交与推送

本仓库提交身份：

```powershell
git config user.name 'Asuka'
git config user.email '25320531@hdu.edu.cn'
```

提交前：

```powershell
git status --short
git diff --stat
git diff --check
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Test-MCUForge.ps1 -Mode Static
```

只暂存本任务文件，检查暂存差异：

```powershell
git add <明确文件列表>
git diff --cached --stat
git diff --cached
git commit -m 'type: concise description'
```

推送前核对远端：

```powershell
git remote -v
git push github HEAD
git ls-remote github HEAD
```

不要强推，不要把别人的未提交文件顺手加入提交，也不要用新分支掩盖错误基线。

## 11. 文档同步规则

代码行为变化时至少检查：

- 根 `README.md` 的快速启动是否仍有效；
- `docs/ARCHITECTURE.md` 的组件/状态机是否真实；
- `docs/OPERATIONS.md` 的命令和端口是否一致；
- `agent_infra/hiclaw/README.md` 的脚本参数是否一致；
- MCP README 的工具、边界和错误是否一致；
- `docs/VALIDATION.md` 是否需要新的证据日期。

文档不允许把规划功能写成已经实现。尚未验证的内容必须显式标注。

## 12. 发布代码包

完成提交与推送后：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Build-MCUForgePackage.ps1
```

用生成的 SHA-256 核对上传文件。比赛 ZIP、PPT、PDF 和视频可以作为平台提交物或 GitHub Release 附件；不要把超大视频和可再生缓存塞进源码历史。
