# STM32 Tool Bridge MCP Server

这是运行在 Windows 主机上的受控桥接层，使 HiClaw Worker 能通过 Higress 调用指定工程的工具，而不获得任意 Shell、主机文件系统、烧录或串口权限。工程与 Demo Profile 由 `ProjectRoot`、`ProfileRoot` 显式绑定，不要求它们与 Agent 运行时同级。

## 当前工具

| Tool | 行为 | 边界 |
| --- | --- | --- |
| `stm32_get_project_snapshot` | Git 分支、HEAD、状态和文件数 | 只读，不访问远端 |
| `stm32_list_project_files` | 分页列出 Git 跟踪文件 | 不暴露生成物和依赖目录 |
| `stm32_read_project_file` | 分页读取受限文本文件 | 拒绝越界、密钥和大文件 |
| `stm32_get_git_diff` | 读取未暂存差异 | 不 stage/reset/commit/fetch |
| `stm32_verify_test_integrity` | 校验固定测试哈希 | 不修改测试或锁 |
| `stm32_run_keil_build` | 调用固定 Keil 包装脚本 | 不烧录、不访问串口、不执行任意命令 |
| `stm32_create_patch_proposal` | 校验并登记 Agent 统一 diff | 只写入 `ProfileRoot/patch_proposals/<id>/`，不写工程源码 |
| `stm32_apply_approved_patch` | 复核后将已批准提案加入 Git 暂存区 | 默认必须提供人类复制的 `APPLY` 令牌；显式启用本地自动模式后可用 `AUTO` 令牌；不提交、推送、烧录或操作 COM |

## 本机启动

```powershell
cd agent_infra\tool_bridge\stm32-mcp-server
npm install
npm run build
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Start-STM32ToolBridge.ps1
```

需要一次确认后自动完成本地补丁应用、构建和固定测试时，改用：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Start-STM32ToolBridge.ps1 -EnableAutonomousLocalMode
```

默认绑定 `demos\um10550-board-demo\firmware` 与其 `agent_profile`；其他工程必须在启动时显式传入 `-ProjectRoot`、`-ProfileRoot`。首次启动会在 `%LOCALAPPDATA%\MCUForge\stm32-tool-bridge.token` 生成 256 位随机令牌。令牌不写入仓库，也不应复制到聊天记录；Higress 代理保存上游认证信息，Worker 只持有自己的网关 Consumer Key。

健康检查：`http://127.0.0.1:8765/health`。MCP 端点为 `http://host.docker.internal:8765/mcp`，必须通过 Bearer 令牌认证。

桥接服务启动后，把它注册到 HiClaw 的 Higress MCP Proxy，并只授权 Manager、Leader、Firmware、Verification Worker：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Configure-HiClawProxy.ps1
```

该脚本从现有 `C:\Users\hz_wu\hiclaw-manager.env` 安全读取控制台登录信息，不打印密码、Consumer Key 或桥接令牌；授权列表采用完整替换并固定为 Manager、Leader、Firmware、Verification 四个 Consumer。为兼容当前 Higress 镜像不注入上游自定义头的行为，脚本只把这些 Consumer Key 的 SHA-256 写入 `%LOCALAPPDATA%\MCUForge\stm32-tool-bridge-consumer-hashes.json`，桥接服务不会保存明文 Key。

## 受控补丁流程

Firmware 通过 `stm32_create_patch_proposal` 提交统一 diff。桥接层会调用 `patch_channel/New-MCUForgePatchProposal.ps1`，重新检查冻结策略、Git 基线、路径白名单、文件哈希和 `git apply --check`，只把不可覆盖的 `proposal.patch` 与 `proposal.json` 写到 Profile 的审计目录；工程源码和暂存区不会改变。

默认模式下，人类在 Windows 上审阅这两个文件后，把 `proposal.json` 中的精确令牌复制到 Team 消息中，明确要求执行。启用 `-EnableAutonomousLocalMode` 后，用户在本次需求确认中写出 `AUTO_LOCAL`，Leader 可使用 `AUTO <proposal_id> <sha256>` 自动应用提案。两种模式都会再次检查当前 HEAD、源码哈希、策略哈希和补丁哈希，最后只执行 `git apply --index`，留下 `apply-record.json`。

提案登记会记录 `baseline_validation.mode`。如果历史只是因 Agent 文档/配置提交或仓库拆分而漂移，且 `patch-policy.json` 中记录的允许路径源码哈希全部一致，工具会按非源码历史漂移继续；如果源码哈希变化、策略 JSON 不完整或历史分叉无法证明源码未变，才返回 `TOOLING_BLOCKED`/策略错误并保持工程不变。不能修改旧提案或绕过检查。

## 安全边界

- 服务只接受明确注册的工具及固定参数；没有通用命令执行接口。
- 工程路径由启动参数固定；所有读取都经过越界、目录、扩展名、大小和疑似密钥检查。
- Keil 构建会更新可再生构建产物，因此不是纯只读，但它不能烧录或修改源码。
- 补丁提案写入仅限 Profile 审计目录；应用补丁也只会进入 Git 暂存区。烧录、串口硬件测试和 Git push 仍未暴露，必须分别设计审批凭据和审计记录。

## 验证

- `npm run test:client`：本机鉴权、工具发现、工程快照和固定测试完整性冒烟。
- `evaluations/read-only.xml`：10 个基于冻结历史证据的只读评测题，答案可直接字符串比较；不需要构建、烧录、串口或写操作。
