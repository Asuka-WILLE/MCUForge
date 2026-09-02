## MCUForge 项目房间唯一工作区策略

当人类管理员用自然语言表达“创建、新建、启动、另开一个项目”时，必须在当前
turn 立即执行 `project-room-lifecycle` Skill。不要先在私聊中拆解任务，也不要询问
是否需要建房或邀请成员。

1. 默认把 `mcuforge-lead`、`mcuforge-requirements`、`mcuforge-research`、
   `mcuforge-firmware`、`mcuforge-verification` 全部加入新项目房间；只有管理员明确
   要求缩减团队时才可减少。
2. 创建脚本返回 `READY` 前，不得派发项目任务。返回 `BLOCKED` 时按同一
   `project_id` 幂等重试，不得换 ID 重复建房。
3. 初始私聊或 Team 房间只允许回复一次迁移通知，包含 `project_id`、房间名称和
   `room_id`。原始需求、澄清、计划确认、派工、进度、阻塞、验收和结项必须全部
   写入该项目房间。
4. 禁止在 Worker 私有房间派发属于项目的任务。需要私密凭据时只引用安全存储位置，
   不得把凭据复制到任何 Matrix 房间。
5. 每条任务消息必须携带 `project_id`、`task_id` 和真实 `m.mentions`；每次状态变化
   必须在原项目房间汇报。心跳只补漏，不承担正常推进。
6. 重复收到同一项目请求时，先检查 `shared/projects/*/meta.json`；已有
   `planning/active/blocked` 项目则恢复原房间，不得创建第二个房间。
7. 创建脚本必须获得当前 `source_room_id`，由脚本读取管理员原始消息并生成
   `request.md` 与 `[ORIGINAL_REQUEST]` 事件。缺少原始需求证据时不得进入 `READY`。
8. 创建失败后的恢复只能使用同一 `project_id`、`source_room_id` 和
   `source_event_id` 重新运行创建脚本。禁止手工改写 `lifecycle_state=READY`，禁止用
   模型消息替代脚本应登记的事件 ID、哈希和审计记录。

此策略优先于内置 `project-management` 中“回到 DM 确认计划”的旧规则。
