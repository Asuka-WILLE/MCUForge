# STM32 Tool Bridge MCP Server

这是运行在 Windows 主机上的受控桥接层，使 HiClaw Worker 能通过 Higress 调用真实工程工具，而不获得任意 Shell、主机文件系统、烧录或串口权限。

## 当前工具

| Tool | 行为 | 边界 |
| --- | --- | --- |
| `stm32_get_project_snapshot` | Git 分支、HEAD、状态和文件数 | 只读，不访问远端 |
| `stm32_list_project_files` | 分页列出 Git 跟踪文件 | 不暴露生成物和依赖目录 |
| `stm32_read_project_file` | 分页读取受限文本文件 | 拒绝越界、密钥和大文件 |
| `stm32_get_git_diff` | 读取未暂存差异 | 不 stage/reset/commit/fetch |
| `stm32_verify_test_integrity` | 校验固定测试哈希 | 不修改测试或锁 |
| `stm32_run_keil_build` | 调用固定 Keil 包装脚本 | 不烧录、不访问串口、不执行任意命令 |

## 本机启动

```powershell
cd agent_infra\tool_bridge\stm32-mcp-server
npm install
npm run build
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Start-STM32ToolBridge.ps1
```

首次启动会在 `%LOCALAPPDATA%\MCUForge\stm32-tool-bridge.token` 生成 256 位随机令牌。令牌不写入仓库，也不应复制到聊天记录；Higress 代理保存上游认证信息，Worker 只持有自己的网关 Consumer Key。

健康检查：`http://127.0.0.1:8765/health`。MCP 端点为 `http://host.docker.internal:8765/mcp`，必须通过 Bearer 令牌认证。

桥接服务启动后，把它注册到 HiClaw 的 Higress MCP Proxy，并只授权 Firmware/Verification Worker：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Configure-HiClawProxy.ps1
```

该脚本从现有 `C:\Users\hz_wu\hiclaw-manager.env` 安全读取控制台登录信息，不打印密码、Consumer Key 或桥接令牌；授权列表采用完整替换并固定为 Manager、Firmware、Verification 三个 Consumer。为兼容当前 Higress 镜像不注入上游自定义头的行为，脚本只把这些 Consumer Key 的 SHA-256 写入 `%LOCALAPPDATA%\MCUForge\stm32-tool-bridge-consumer-hashes.json`，桥接服务不会保存明文 Key。

## 安全边界

- 服务只接受明确注册的工具及固定参数；没有通用命令执行接口。
- 工程路径由启动参数固定；所有读取都经过越界、目录、扩展名、大小和疑似密钥检查。
- Keil 构建会更新可再生构建产物，因此不是纯只读，但它不能烧录或修改源码。
- 烧录、串口硬件测试、Git push 和源码写入仍然没有暴露；后续必须分别设计审批凭据和审计记录。

## 验证

- `npm run test:client`：本机鉴权、工具发现、工程快照和固定测试完整性冒烟。
- `evaluations/read-only.xml`：10 个基于冻结历史证据的只读评测题，答案可直接字符串比较；不需要构建、烧录、串口或写操作。
