# 任务：实现 MCUForge Demo 安全状态机

## 当前基线

电脑通过 USB CDC 每 20 ms 发送控制帧，开发板解析后计算虚拟左右输出并显示在 TFT；该路径已经与 SBUS 和 RS485 隔离。基础版本故意保持最后一条命令，也不会执行急停请求。

## 必须实现

1. 最后一个有效控制帧的年龄超过 150 ms 时进入 `FAILSAFE`，`left_cmd=0`、`right_cmd=0`、`cmd_valid=0`。
2. `FAILSAFE` 下运动帧不能恢复控制；必须连续收到 3 个有效中位帧后才退出。
3. 急停 flag 一旦出现，立即进入锁定 `ESTOP` 并清零；恢复规则由 Requirement Agent 根据用户确认冻结，不能自行猜测。
4. TFT、USB JSON 和 PC GUI 对 `RUN`、`FAILSAFE`、`ESTOP` 的显示必须一致。
5. JSON 遥测提供 `pc_recovery_neutral_count`，使验证 Agent 能证明第二帧仍锁定、第三帧才恢复。
6. 保持 Demo 路径不调用任何 SBUS、RS485、电机和升降函数。

## 验收

- Keil `UM10550` 全量构建：0 errors、0 warnings。
- Python 编译与协议单元测试通过。
- 固定测试哈希与 `agent_infra/testcase-lock.json` 一致。
- `CTRL-001`、`FS-001`、`REC-001`、`ESTOP-001` 全部通过。
- 每个硬件测试保留 `result.json` 和原始 `telemetry.jsonl`。

## 人工审批点

烧录和远端推送均不属于 Agent 的默认权限。Verification Agent 必须在这两个步骤前分别向用户申请。
