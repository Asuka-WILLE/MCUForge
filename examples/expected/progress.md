# 预期的进度与结果格式

每次状态变化应先有机器可读字段：

```text
[PROGRESS] run_id=MCUFORGE-... stage=IMPLEMENTATION state=IN_PROGRESS
done=已读取冻结合同和工程快照
current=正在生成白名单内补丁提案
next=登记提案并交给 Verification
evidence=task-contract.yaml sha256=...
```

随后紧跟中文解释：

```text
【中文进度】阶段：实现｜状态：进行中
已完成：已读取冻结合同和真实工程快照。
当前：正在生成白名单范围内的补丁提案。
下一步：登记提案并交给 Verification 独立检查。
证据：task-contract.yaml SHA-256、工程 HEAD。
```

最终结果必须把以下层级分开：

- 补丁是否只是提案，还是已真正应用；
- 工作树/暂存区 diff 是否非空；
- 固定测试文件哈希是否通过；
- Keil 是否真实全量构建，错误和警告数量；
- HEX/AXF 是否存在及其 SHA-256；
- 是否真实烧录；
- 是否运行 COM/硬件测试；
- 哪些内容未验证及原因。

任何 `BLOCKED` 都要给出最早失败证据、已停止的重试和一个明确下一步，不能只说“卡住了”。
