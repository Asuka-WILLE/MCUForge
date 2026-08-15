# MCUForge-FS-001 共享上下文包

这是 `MCUFORGE-FS-001` 的唯一可发布输入包。它让 Requirement、Research、Firmware、Verification 和 Team Leader 使用同一份冻结需求与工程事实，而不是在聊天中各自复述、各自猜测。

## 发布前提

1. 本仓库工作树必须干净；上下文描述的是一个明确 Git 提交，而不是混有未提交改动的目录。
2. `task-contract.yaml` 只能由 Requirement Agent 在人类确认后冻结；若需求变了，创建新的 `run_id`，不改旧 run。
3. 固定测试只读。测试哈希和断言不随实现修改。
4. 网络资料、器件手册和例程在本 run 中尚未冻结；Research Agent 若需要它们，必须输出新的来源包或请求用户提供资料。

## 目录与同步

使用 `agent_infra/hiclaw/Publish-MCUForgeSharedContext.ps1` 发布后，所有 Worker 从下面位置读取：

```text
/root/hiclaw-fs/shared/mcuforge/runs/MCUFORGE-FS-001/
```

发布脚本会在该目录额外生成 `publish-manifest.json`，其中记录发布 Git 提交、文件哈希和内容 ID。该清单是 Verification Agent 判断交接输入是否被替换的依据。

## 本包包含

- `task-contract.yaml`：冻结验收合同、范围、不可变测试和人工审批点。
- `project-context.md`：目前工程、Demo 隔离、协议与相关文件的可追溯事实。
- `source-register.yaml`：本 run 已允许引用的本地证据与尚缺资料。

## 交接规则

每个 Agent 在交接消息中都必须写：`run_id`、`publish_manifest_sha256`、已读取文件、结论、未决问题和下一角色。缺其中任意项的交接不能作为实现或验收依据。
