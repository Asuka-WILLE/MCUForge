# MUC_AGENT

MUC_AGENT 是面向嵌入式与单片机项目的协作式开发 Agent 平台。它不是某一辆车或某一块开发板的控制程序：用户给出需求后，Agent 团队会先冻结验收合同、检索公开手册与例程、生成受限补丁提案，并用真实构建和固定测试给出可审计结论。

## 它解决什么

- **接手已有工程**：读取项目结构、手册、历史证据和固定测试，而不是凭聊天猜测。
- **启动新工程**：Research Agent 仅从公开允许名单检索官方手册、GitHub、Gitee、CSDN 等技术来源；资料缺失时明确要求用户提供。
- **安全协作开发**：需求、研究、固件和验证由不同角色负责；Agent 不直接烧录、推送或操作串口。
- **可演示的交付**：补丁必须经过路径白名单、哈希、人工批准和真实构建；失败证据同样保留。

## 仓库结构

```text
MUC_AGENT/
├── agent_infra/                  # 可复用的 HiClaw 团队、MCP 桥、Skills、补丁审批通道
│   ├── hiclaw/
│   ├── tool_bridge/
│   ├── skills/
│   └── patch_channel/
└── demos/
    └── um10550-board-demo/       # 一个可运行的示例，不是平台本体
        ├── firmware/             # 完整 STM32/Keil 工程、PC 控制工具、固定测试和证据
        └── agent_profile/        # 该示例的合同、角色、测试哈希与补丁策略
```

所以，**测试文件属于 Demo**，不是 `agent_infra` 的同级产品代码；`agent_infra` 通过显式 `ProjectRoot` 和 `ProfileRoot` 绑定到任一示例或用户工程。

## 当前可运行能力

1. HiClaw Team：Leader、Requirement、Research、Firmware、Verification 五个角色协作。
2. 受控 Research MCP：仅允许公开 HTTPS 技术资料搜索与读取，禁止登录、下载、执行、私网访问和仓库写入。
3. STM32 MCP：仅提供项目读取、Git diff、固定测试完整性检查和 Keil 构建。
4. 受控补丁通道：Firmware 只能提交统一 diff；人类审阅并输入精确令牌后，才会加入 Git 暂存区。

## 从 UM10550 示例开始

示例入口在 [demos/um10550-board-demo](demos/um10550-board-demo/README.md)。它使用开发板、TFT、USB CDC 和电脑虚拟手柄，不需要车、遥控器或电机。

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File agent_infra\hiclaw\Bootstrap-MCUForgeTeam.ps1 -EnableToolBridge -EnableResearchBridge
pwsh -NoProfile -ExecutionPolicy Bypass -File agent_infra\tool_bridge\stm32-mcp-server\Start-STM32ToolBridge.ps1
```

运行 Team 或桥接服务不会写入固件、不会烧录、不会打开 COM 口，也不会推送远端；这些动作始终需要单独的人工批准。
