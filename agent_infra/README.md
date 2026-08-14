# MCUForge Agent Infra

本目录把比赛 Demo 的“多 Agent 协作”变成可执行约束，而不是角色介绍 PPT。固定输入是任务规格、串口协议、不可随实现修改的黑盒测试及其哈希；固定输出是源码差异、Keil 构建结果、COM3 测试结果和证据审计结论。

## 四个职能 Agent

| Agent | 主要职责 | 可修改范围 | 禁止事项 |
| --- | --- | --- | --- |
| Requirement Agent | 把用户目标转换为验收合同和字段约定 | 仅 handoff 文档 | 不改功能源码和固定测试 |
| Firmware Agent | 在独立模块中实现 MCU 安全状态机 | `Core/Inc/mcuforge_demo.h`、`Core/Src/mcuforge_demo.c` | 不改测试，不接入 RS485 |
| PC Tool Agent | 同步 GUI 状态、遥测记录和操作提示 | `PC_Tools/telemetry_monitor.py`、协议文档 | 不改固件，不降低断言 |
| Verification Agent | 锁测试、真实构建、申请烧录、运行黑盒测试、归档证据 | `docs/evidence/` 和验证报告 | 未获用户许可不得烧录；不得修实现 |

角色详细提示位于 [`roles/`](roles/)，最终功能任务位于 [`tasks/implement-demo-safety.md`](tasks/implement-demo-safety.md)。实际运行 AgentTeams 时，协调者应把任务规格和对应角色文件一起交给每个 Agent；Agent 之间只通过可审计 handoff 传递结论。

## 可复用 Skills

- [`stm32-keil-build`](skills/stm32-keil-build/SKILL.md)：真实 Keil 构建并输出机器可读摘要和产物哈希。
- [`stm32-fixed-hardware-test`](skills/stm32-fixed-hardware-test/SKILL.md)：校验固定用例后，通过 USB CDC 运行黑盒测试。
- [`stm32-evidence-audit`](skills/stm32-evidence-audit/SKILL.md)：检查修改边界、测试完整性、构建/硬件证据和禁止事项。

## 闭环顺序

1. Requirement Agent 冻结验收合同，不写实现。
2. Firmware Agent 与 PC Tool Agent 根据同一合同分别修改自己的文件。
3. Verification Agent 先运行测试哈希锁和静态检查，再执行 Keil 全量构建。
4. 到达烧录点必须暂停并取得用户明确许可。
5. 烧录后运行 `CTRL-001`、`FS-001`、`ESTOP-001`、`REC-001`，保存原始遥测和 `result.json`。
6. Verification Agent 只给出通过/拒绝；失败回传给相应实现 Agent，不能替它改代码。

当前基础设施已经烧录并取得硬件基线：`CTRL-001` 通过，`FS-001`、`ESTOP-001`、`REC-001` 按预期失败。AgentTeams 接下来必须真正实现安全状态机，再用同一组固定测试复测，不能修改测试来迎合结果。
