# 更新日志

本文件记录 MCUForge 竞赛迁移期间的可审计变更。日期采用 `YYYY-MM-DD`；构建、测试、烧录结论必须以对应证据文件为准。

## [未发布] - 2026-08-14

### 新增

- 建立当前 STM32 工程地图：[`docs/current-project-map.md`](docs/current-project-map.md)。
- 建立现有上位机与遥测兼容性地图：[`docs/current-pc-tool-map.md`](docs/current-pc-tool-map.md)。
- 记录无源码改动的 Keil 基线构建结果：[`docs/evidence/baseline-build-2026-08-14.md`](docs/evidence/baseline-build-2026-08-14.md)。

### 已验证

- 在本地分支 `feature/mc02-bmi088-interface`、基线提交 `b4adf58` 上执行 Keil `UM10550` target 构建；退出码为 0，Keil 报告为 `0 Error(s), 0 Warning(s)`。
- 记录了本次生成的 HEX 与 AXF 的 SHA-256，供后续烧录审批和证据归档比对。

### 约束与未做事项

- 本次仅完成《项目要求》第 4 节迁移后只读检查的第 1–9 项及其记录；未修改任何 C/C++、CubeMX、Keil、Python 功能代码。
- 未访问任何远端仓库，未推送、拉取或烧录固件。
- 现有固件会通过 RS485 控制真实轮毂电机；竞赛 Demo 接入 PC 控制前，必须先实现独立的虚拟输出路径，不能把比赛控制帧接到现有真实电机写入链路。
- 尚未执行 FS-001 至 FS-007，也尚未创建 `baseline-no-failsafe` 标签；这两项必须在虚拟控制基线和固定 CLI 测试器完成后进行。
