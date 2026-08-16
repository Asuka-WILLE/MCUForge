# MUC_AGENT

MUC_AGENT 是面向嵌入式与单片机项目的协作式开发 Agent 平台。它不是某一辆车或某一块开发板的控制程序：用户给出需求后，Agent 团队会先冻结验收合同、检索公开手册与例程、生成受限补丁提案，并用真实构建和固定测试给出可审计结论。

> **第一次使用或准备把本仓库交给其他人？** 请先阅读 [HiClaw 完整上手手册](agent_infra/hiclaw/README.md)。其中包含环境要求、Docker/HiClaw 初始化、两个 MCP Bridge 的启动与注册、发任务方式、补丁审批、重启和排错；不要只运行本页的一两条命令。

## 它解决什么

- **接手已有工程**：读取项目结构、手册、历史证据和固定测试，而不是凭聊天猜测。
- **启动新工程**：Research Agent 仅从公开允许名单检索官方手册、GitHub、Gitee、CSDN 等技术来源；资料缺失时明确要求用户提供。
- **安全协作开发**：需求、研究、固件和验证由不同角色负责；协作模式下角色可以共享工程与资料读取能力，但 Agent 仍不直接烧录、推送或操作串口。
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
    └── vcw-board-demo/       # 一个可运行的示例，不是平台本体
        ├── firmware/             # 完整 STM32/Keil 工程、PC 控制工具、固定测试和证据
        └── agent_profile/        # 该示例的合同、角色、测试哈希与补丁策略
```

所以，**测试文件属于 Demo**，不是 `agent_infra` 的同级产品代码；`agent_infra` 通过显式 `ProjectRoot` 和 `ProfileRoot` 绑定到任一示例或用户工程。

## 当前可运行能力

1. HiClaw Team：Leader、Requirement、Research、Firmware、Verification 五个角色协作。
2. 受控 Research MCP：仅允许公开 HTTPS 技术资料搜索与读取，禁止登录、下载、执行、私网访问和仓库写入；协作模式下 Leader、Requirement、Research 都可以调用。
3. STM32 MCP：提供项目读取、Git diff、固定测试完整性检查、Keil 构建，以及受控补丁提案和应用；协作模式下五个角色都能读取工程与证据。
4. 受控补丁通道：默认由人类审阅并输入精确令牌后，Leader 才能把 Firmware 的统一 diff 加入 Git 暂存区；可选 `AUTO_LOCAL` 模式允许一次确认后自动完成本地应用、构建和固定测试。
5. 实时进度协议：每个角色在接单、关键工具调用前后、等待、阻塞和完成时发布 `[PROGRESS]`，Leader 每 5 分钟发送一次心跳。

## 从 VCW 示例开始

示例入口在 [demos/vcw-board-demo](demos/vcw-board-demo/README.md)。它使用开发板、TFT、USB CDC 和电脑虚拟手柄，不需要车、遥控器或电机。完整启动顺序在 [HiClaw 完整上手手册](agent_infra/hiclaw/README.md#5-首次启动按顺序执行)：先启动两个本机 Bridge，再注册 MCP、创建 Team 并验证五个角色。

运行 Team 或桥接服务不会写入固件、不会烧录、不会打开 COM 口，也不会推送远端；这些动作始终需要单独的人工批准。

## License

除另有声明的文件或目录外，本仓库的原创 MCUForge 代码及已获相应著作权人授权的 VCW Demo 内容，均按 [Apache License 2.0](LICENSE) 发布。VCW Demo 的原有著作权声明应继续保留。

仓库中包含 Arm CMSIS、STM32 HAL 和 STM32 USB Device Library 等第三方组件；它们继续适用各自目录内的原许可证，根目录 Apache-2.0 不会覆盖这些条款。完整清单见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)，署名说明见 [NOTICE.md](NOTICE.md)。
