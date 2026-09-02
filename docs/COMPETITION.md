# 世界人工智能开源大赛：要求与 MCUForge 映射

## 1. 赛道定位

MCUForge 参加“Agent Infra 新智基座”赛道。官方页面：

- [Agent Infra - 世界人工智能开源大赛](https://www.goaihz.com/tracks?track=infra)

截至 2026-09-03，官网对该赛道的核心描述是：聚焦企业级复杂任务下的多 Agent 基础设施与协同系统，推动 Agent 从 Demo 走向 Production。参赛者应从真实行业场景出发，构建可复用 Skill、工具集成和运行验证能力，并形成端到端闭环。

比赛时间和提交字段可能调整，真正提交前必须再看一次官网；本文件用于解释作品设计，不替代官方通知。

## 2. 硬性要求

| 官方要求 | MCUForge 如何满足 | 主要证据位置 |
| --- | --- | --- |
| 以 HiClaw/AgentTeams 为设计基础 | Team、Worker、Manager、Matrix 和 MinIO 均由 HiClaw 承载 | `agent_infra/hiclaw/` |
| 不少于 3 个不同职能 Agent | Lead、Requirement、Research、Firmware、Verification 共 5 个角色 | `demos/vcw-board-demo/agent_profile/hiclaw_roles/` |
| 端到端任务闭环 | 需求草案 → 人工确认 → 合同 → 研究 → 实现 → 验证 → 审批/交付 | `docs/ARCHITECTURE.md`、角色 SOUL |
| Agent Identity 清单 | 每个角色都定义输入、输出、依赖、决策边界和 Trace | 角色 SOUL、项目材料中的 Agent Identity 清单 |
| Skill 是核心必选内容 | 工程原则、Keil 构建、固定硬件测试和证据审计 Skill | `agent_infra/skills/` |
| 工具集成 | 两个 MCP Bridge 加内部 Worker Control Bridge | `agent_infra/tool_bridge/`、`agent_infra/hiclaw/worker-control-bridge/` |
| 上下文传递与共享状态 | Manifest、合同、项目上下文、来源清单、MinIO 项目目录 | `agent_profile/shared_context/`、动态 `shared/projects/` |
| 结果验证 | 固定测试完整性、真实 Keil 构建、diff 和产物哈希 | STM32 MCP、Verification 角色、`docs/VALIDATION.md` |
| 执行证据与安全审计 | Matrix 轨迹、`audit.ndjson`、patch proposal/apply record、哈希 | 项目房间、MinIO、`agent_profile/patch_proposals/` |
| 异常分支 | `BLOCKED`、同 ID 幂等恢复、Worker restart/recreate、失败证据 | 项目房间状态机、Worker Control Bridge |
| Memory/RAG/共享状态/轨迹至少覆盖要求组合 | 共享状态 + 轨迹可观测 + 项目资料沉淀 | MinIO、Matrix、reference/source register |

## 3. 评分项与作品重点

官网当前给出的评价维度为：

| 维度 | 权重 | MCUForge 应重点展示什么 |
| --- | ---: | --- |
| 场景价值与行业可复制性 | 25% | MCU 开发普遍痛点；VCW 只是实例；新芯片/新工程可换 Profile 复用 |
| 多 Agent 协作与闭环 | 25% | 五角色真实交接、确认门、失败返工、项目房间唯一轨迹 |
| Skill 系统与复用价值 | 25% | Skill 合同、触发条件、输入输出、权限边界、版本和回滚 |
| 工程化、验证、安全审计 | 20% | MCP 白名单、补丁哈希、固定测试、Keil 构建、Worker 恢复和证据 |
| 开源贡献 | 5% | Apache-2.0、README、部署指南、示例配置、测试入口和可复用代码 |

不要把 PPT 大部分篇幅用来展示 TFT 画面。硬件只是证明工具真的落地；Agent 协作、Skill 调用、异常恢复和证据才是赛道主体。

## 4. 各阶段提交物

### 初赛设计阶段

官网允许以方案设计为主，重点说明：

- 场景与价值；
- 系统架构和 Agent 分工；
- 任务拆解、上下文和验证；
- Skill、MCP、RAG/共享状态/可观测规划；
- 可行性、落地计划、安全边界、风险和开放计划。

### 复赛可运行阶段

官网要求更新后的方案材料，以及可执行 AgentTeams 代码包和可运行 Demo/Demo 视频。代码包至少应具备：

- 运行入口；
- 依赖声明；
- 示例配置；
- 样例输入/输出；
- 执行证据；
- 明确的复现步骤。

本仓库对应内容：

| 需要的内容 | 仓库位置 |
| --- | --- |
| 环境检查 | `Check-MCUForgeEnvironment.ps1` |
| 安装入口 | `Install-MCUForge.ps1` |
| 启动入口 | `Start-MCUForge.ps1` |
| 验收入口 | `Test-MCUForge.ps1` |
| 依赖锁 | 两个 Bridge 的 `package-lock.json`、PC 工具的 `requirements.txt` |
| 示例配置 | `config/mcuforge.local.example.psd1` |
| 样例输入/输出 | `examples/` |
| 执行证据 | `docs/VALIDATION.md` 和 Demo 内证据 |
| 部署与排障 | `docs/OPERATIONS.md` |

### 最终开源仓库

官网特别关注 README、部署指南、License、示例配置和测试方法。本仓库分别由根 README、`docs/OPERATIONS.md`、`LICENSE`、`config/` 和 `Test-MCUForge.ps1` 提供。

## 5. Demo 视频必须出现的内容

官网要求视频不超过 8 分钟，并至少包含 Agent 协作过程、Skill 调用证据和异常处理演示。推荐 7 分 30 秒结构：

1. **0:00–0:35 痛点**：AI 越权改码、反复测试、缺少复测和手册遗忘。
2. **0:35–1:10 产品与架构**：五个 Agent、Manager、Matrix、MinIO、MCP。
3. **1:10–1:45 原始工程状态**：VCW Demo 当前行为和缺失安全功能。
4. **1:45–2:25 自然语言建项目**：Manager 自动建房、七名成员、原始请求留痕。
5. **2:25–3:10 需求确认门**：Lead 输出 `INTAKE_DRAFT`，用户修改并明确确认。
6. **3:10–4:35 多 Agent 协作**：合同、Research 来源、Firmware 提案、中文进度。
7. **4:35–5:25 Skill/MCP 证据**：工程快照、补丁哈希、固定测试、Keil 构建。
8. **5:25–6:20 异常处理**：模拟 Worker 不回执或错误提案，展示自动回执、恢复或拒绝。
9. **6:20–7:00 最终硬件/PC 结果**：TFT/虚拟手柄/监控变化。
10. **7:00–7:30 审计与复用**：Matrix 轨迹、MinIO 证据、换项目只换 Profile。

视频中不要只快进聊天记录。每个关键结论旁边同时展示工具名、文件名或 SHA，评委才能判断不是口头描述。

## 6. 作品的一句话介绍

MCUForge 是面向单片机研发的多 Agent 工程协作基座：它把自然语言需求转化为可确认合同，由不同角色完成资料研究、模块化实现和独立验证，并通过受控 MCP、哈希、真实构建、实时进度和异常恢复形成可审计闭环。

## 7. 提交前检查清单

- [ ] GitHub 仓库可公开访问，默认分支能看到最新提交。
- [ ] README 第一屏能说明项目、快速启动和文档入口。
- [ ] 从干净克隆执行 Static、Live、Full 验收。
- [ ] 代码包不含密钥、token、Cookie、个人绝对路径和运行缓存。
- [ ] PPT/PDF 与当前实现一致，不再描述未实现功能。
- [ ] Demo 视频小于 8 分钟，包含协作、Skill/MCP、异常和证据。
- [ ] 在线体验地址、账号和密码单独按比赛平台要求提供，不写进公开 Git。
- [ ] Apache-2.0、NOTICE 和第三方许可证齐全。
- [ ] 提交前重新核对官网时间、文件大小和命名要求。
