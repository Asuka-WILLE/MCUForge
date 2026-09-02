# 创建可审计项目

## 触发

管理员一旦表达要创建新项目，立即创建房间。无需先确认房间、成员或是否启动流程。
若需求还不完整，在项目房间内继续澄清。

## 创建

默认使用 MCUForge 全体角色，不传 `--workers`：

```bash
PROJECT_ID="proj-$(date +%Y%m%d-%H%M%S)"
bash /root/manager-workspace/skills/project-room-lifecycle/scripts/create-project.sh \
  --id "${PROJECT_ID}" \
  --title "<简短项目标题>" \
  --source-room "<当前 room_id，可缺省>" \
  --source-event "<当前 event_id，可缺省>"
```

脚本负责：幂等创建目录与 Matrix 房间、邀请并自动加入管理员/Leader/四个 Worker、
验证成员、从来源房间读取管理员原始需求、登记 Manager 房间权限、写入审计日志并
同步 MinIO。`--source-room` 必须传入当前会话 room_id；如果上下文没有显式显示，先
读取 `~/state.json` 的 `admin_dm_room_id`，不得省略后凭记忆概括需求。

## 创建后的唯一允许流程

1. 检查脚本 JSON 结果的 `lifecycle_state` 必须是 `READY`。
2. 在来源房间只发一次：
   `[PROJECT_MOVED] project_id=<id> room_id=<room_id> 后续交流已迁移到项目房间。`
3. 确认脚本已在项目房间发送 `[ORIGINAL_REQUEST]`，且 `meta.json` 中存在
   `original_request_event_id`；不要再用摘要替代原文。
4. 在项目房间完成需求澄清、计划展示和确认；不得返回 DM 等待确认。
5. 计划确认后更新 `meta.json.status=active` 与 `confirmed_at`，同步 MinIO，再派工。

## 重试与恢复

- 同一 `project_id` 重跑必须复用原 `project_room_id`，不得产生重复房间。
- 恢复必须重新运行上述脚本，并沿用 `meta.json` 记录的原始
  `source_room_id/source_event_id`；不得手工把元数据改成 `READY`。
- `BLOCKED` 时在来源房间和已有项目房间报告阶段、错误和下一动作。
- 如果成员未全部 `join`，不得进入 `READY`，也不得开始派工。
- `READY` 前必须同时存在 `request.md`、原始请求 SHA、
  `original_request_event_id` 和 `source_notice_event_id`；任一缺失即保持 `BLOCKED`。
- Manager 重启后从 `shared/projects/<project-id>/meta.json` 恢复房间绑定。

## 元数据契约

`meta.json` 至少包含：`schema_version`、`project_id`、`title`、
`project_room_id`、`interaction_mode`、`lifecycle_state`、`status`、`workers`、
`participants`、`source_room_id`、`source_event_id`、`created_at`、
`room_ready_at`、`confirmed_at`、`last_error`。
