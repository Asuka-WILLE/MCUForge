# MUC_AGENT Runtime

这里是可复用的 Agent 运行时，不保存某个具体单片机项目的源码、固定测试或验收合同。

- `hiclaw/`：Team 创建、角色加载和共享上下文发布。
- `tool_bridge/`：受控 STM32/Research MCP 服务，不提供任意 Shell；源码只能通过冻结策略和人类精确令牌进入 Git 暂存区。
- `skills/`：Keil 构建、固定测试和证据审计入口。
- `patch_channel/`：只登记和应用经过人工精确批准的统一 diff。

`skills/general-engineering-principles-2026-08-16/` 保存可分发的 `.skill` 包；由 `hiclaw/Install-MCUForgeSkills.ps1` 校验、发布到 MinIO 并同步至 Team Leader 和全部 Worker。Skill 只提供决策与交付规范，不扩大工具权限。

具体项目放在仓库的 `demos/<demo>/` 或用户外部工作区。每次运行都显式绑定：

- `ProjectRoot`：要读取、构建或验证的固件工程；
- `ProfileRoot`：该工程的冻结合同、角色、来源、测试哈希和补丁策略。

当前内置 Profile 是 [`../demos/um10550-board-demo/`](../demos/um10550-board-demo/README.md)。新项目只需要提供同类 Profile；手册或例程缺失时，Research Agent 会停止并要求用户提供资料，而不会捏造依据。
