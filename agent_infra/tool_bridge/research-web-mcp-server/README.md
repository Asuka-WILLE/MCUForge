# MCUForge Research Web MCP Server

这是 MCUForge 的公开技术资料检索桥。基础模式只提供给 `mcuforge-research`；协作模式还提供给 Leader 和 Requirement。它支持定位和读取 GitHub、Gitee、CSDN、ST、Arm 等白名单站点的公开 HTTPS 文本；不提供任意 Shell、登录、Cookie、下载、执行、内网访问、仓库写入或硬件控制。

## 工具

| Tool | 行为 | 关键边界 |
| --- | --- | --- |
| `research_get_policy` | 返回固定来源、大小、速率和引用规则 | 无网络、只读 |
| `research_search_sources` | Bing RSS 检索后只返回白名单结果 | 查询最多 200 字符，最多 10 条 |
| `research_fetch_allowed_source` | 读取白名单 HTTPS 文本并给出内容哈希 | 最大 1 MiB、15 秒、最多 4 次同白名单跳转 |

每条外部事实必须记录：最终 URL、获取时间、内容 SHA-256、具体章节或代码位置、许可证结论。找不到原厂资料、来源不在白名单或许可证不清时，Research Agent 必须标记 `blocked` 并向用户索要资料。

## 本机启动

```powershell
cd agent_infra\tool_bridge\research-web-mcp-server
npm install
npm run build
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Start-ResearchWebBridge.ps1
```

首次启动会在 `%LOCALAPPDATA%\MCUForge\research-web-bridge.token` 生成本机令牌；令牌不进入仓库和聊天记录。健康检查为 `http://127.0.0.1:8766/health`。

## 接入 HiClaw

服务启动后执行：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Configure-HiClawResearchProxy.ps1
```

该脚本默认只授权 `worker-mcuforge-research` Consumer；加上 `-EnableWideAgentAccess` 后还会授权 Leader 和 Requirement，并将所有授权 Consumer 的 Key SHA-256 写入 `%LOCALAPPDATA%\MCUForge\research-web-bridge-consumer-hashes.json`。Firmware 和 Verification 仍通过 Research 的来源包获取资料，不直接联网。
