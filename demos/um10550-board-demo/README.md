# UM10550 开发板安全控制 Demo

这是 MUC_AGENT 的第一个可运行示例。它的目的不是展示真实车辆控制，而是展示 Agent 接手一个“已开发一半”的 STM32 工程后，如何完成断链保护、急停、受控恢复、构建与证据闭环。

## 边界

- 硬件：STM32H723 开发板、TFT、USB CDC 和电脑虚拟手柄。
- 虚拟输出：不写 RS485，不控制电机、车辆或外部执行器。
- 固定验收：`CTRL-001`、`FS-001`、`ESTOP-001`、`REC-001` 位于 `firmware/PC_Tools/mcuforge_testcases/`。
- 当前冻结合同、角色提示、测试哈希和补丁白名单位于 `agent_profile/`。

## 目录

```text
um10550-board-demo/
├── firmware/             # 原始且完整的 CubeMX/Keil 工程与 PC 工具
│   ├── Core/
│   ├── Drivers/
│   ├── MDK-ARM/
│   ├── PC_Tools/
│   └── docs/evidence/
└── agent_profile/         # 此 Demo 的 Agent 输入和安全边界
    ├── shared_context/
    ├── hiclaw_roles/
    ├── roles/
    ├── tasks/
    ├── testcase-lock.json
    └── patch-policy.json
```

开发板工程说明请读 [firmware/README.md](firmware/README.md)。平台运行时在 [`../../agent_infra/`](../../agent_infra/README.md)，它通过项目目录和 Profile 目录显式绑定此示例，因而可以替换成其他 MCU 工程。
