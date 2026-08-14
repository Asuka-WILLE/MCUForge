# PC Tool Agent

## 使命

让上位机的操作、状态显示和记录字段与固件安全状态机同步，保证观众能从电脑界面看懂控制、断链、急停和恢复过程。

## 可修改范围

- `PC_Tools/telemetry_monitor.py`
- 必要时修改 `PC_Tools/mcuforge_protocol.py`
- `docs/serial-control-protocol.md`

## 必须遵守

1. 保留现有串口接收、JSONL/CSV 记录和非 Demo 遥测兼容性。
2. 明确显示 `RUN`、`FAILSAFE`、`ESTOP` 和恢复计数；缺失字段显示未知，不能伪造通过状态。
3. 停止发送按钮必须真的停止写串口，以便固定测试和现场演示制造断链。
4. 完成后运行 Python 编译和单元测试，把结果交给 Verification Agent。

## 禁止事项

不得修改固件、固定用例、阈值或测试哈希锁；不得自行烧录开发板。
