# MCUForge Demo 基础设施构建证据（2026-08-14）

## 范围

- 仓库：`C:\Users\hz_wu\Desktop\GOAI\wheel_control-main\wheel_control-main`
- 分支：`feature/mcuforge-demo-infra`
- 父提交：`dbca8f78b9b68bce912d404296d5e830e58887c3`
- Keil target：`VCW`
- 模式：`MCUFORGE_DEMO_MODE=1`
- 硬件操作：未烧录；未向 COM3 发送新协议控制帧

## 真实全量构建

执行方式等价于：

```powershell
C:\Users\hz_wu\AppData\Local\Keil_v5\UV4\UV4.exe -r MDK-ARM\VCW.uvprojx -t VCW
```

结果：

- µVision 进程退出码：`0`
- `main.c`、`mcuforge_demo.c`、`usbd_cdc_if.c` 均实际参与编译
- Keil 报告：`0 Error(s), 0 Warning(s)`
- Program Size：`Code=64130 RO-data=15174 RW-data=256 ZI-data=10528`

## 构建产物

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `MDK-ARM/VCW/VCW.hex` | 223837 | `4F4FA7C1F67FB8EFC39907D3E645E61A05FBFF6D093134995EB7E98531CDD910` |
| `MDK-ARM/VCW/VCW.axf` | 721748 | `E80F60B5930A57C01098F6A82F72C5AC26146C3C8A7682C5FB1BA780E5796723` |

HEX 是下一步申请烧录的准确固件。AXF 可能包含构建时间等非功能元数据，本记录只用于本次本机构建追踪。

## Python 静态验证

执行：

```powershell
python -m py_compile PC_Tools\mcuforge_protocol.py PC_Tools\mcuforge_test_runner.py PC_Tools\telemetry_monitor.py PC_Tools\tests\test_mcuforge_protocol.py PC_Tools\tests\test_mcuforge_test_runner.py
python -m unittest discover -s PC_Tools\tests -v
python PC_Tools\mcuforge_test_runner.py --list
```

结果：

- Python 文件均可编译；
- 7 个单元测试全部通过：4 个协议测试覆盖 Modbus CRC 标准向量、固定帧向量、范围校验和损坏 CRC 拒绝；3 个测试证明 `CTRL-001` 能识别正确混控、`FS-001` 会拒绝保持末次输出的基线并接受正确清零状态；
- 固定测试器成功加载 `CTRL-001`、`FS-001`、`ESTOP-001`、`REC-001`。

## 尚未证明的内容

本文件只证明源代码可构建和 PC 侧逻辑可静态执行，不证明新固件已在开发板运行。下一步必须由用户明确批准烧录，然后用 COM3 依次取得：

1. TFT 从旧 `Waiting SBUS frame...` 切换到 `MCUFORGE DEMO BASE` 的照片；
2. `CTRL-001` 通过证据；
3. `FS-001` 在基础版本上的预期失败证据。
