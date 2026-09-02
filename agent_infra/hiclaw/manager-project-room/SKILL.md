---
name: project-room-lifecycle
description: 当管理员用自然语言要求创建、新建、启动或另开一个多 Agent 项目时使用；自动创建唯一项目房间、拉入 MCUForge 全体角色，并将需求、确认、派工、进度、异常和结项固定在该房间留痕。
---

# MCUForge 项目房间生命周期

先阅读 `references/create-project.md`，再执行其中脚本。项目房间是项目交互的唯一
事实来源；初始私聊只用于通知迁移结果。

状态机为：`CREATING → ROOM_CREATED → MEMBERS_READY → READY`。任一步失败进入
`BLOCKED`；使用同一 `project_id` 重试，禁止通过更换 ID 绕过失败。

`meta.json`、`plan.md` 与 `audit.ndjson` 必须同步到 MinIO。`meta.json` 的
`schema_version=2`、`interaction_mode=project_room_only` 和 `project_room_id` 是
下游派工前的硬性验收条件。
