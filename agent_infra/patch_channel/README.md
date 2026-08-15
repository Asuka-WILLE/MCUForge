# MCUForge 受控补丁通道

这一通道把“Agent 建议代码”与“改动真实工程”分开：Agent 只能交付统一 diff 文本；本机脚本只会先登记、校验并生成审计提案；只有人类输入精确审批令牌后，脚本才把补丁加入 Git 暂存区。它不会自动提交、推送、烧录或操作 COM 口。

## 固定边界

- 当前冻结任务：`MCUFORGE-FS-001`。
- 只允许修改既有 `Core/Src/mcuforge_demo.c` 与 `Core/Inc/mcuforge_demo.h`。
- 禁止新增、删除、重命名、复制、二进制补丁，以及任何固定测试、合同或证据审计脚本的变更。
- `patch-policy.json` 是本通道的机器可读策略；变更源码范围或基线时，必须重新冻结合同与策略，不能直接改旧提案。

## 人机交接流程

1. Firmware Agent 先读取冻结合同、研究来源和项目快照；只在共享上下文中交付 `proposal.patch`、修改说明和待验证假设，不写 Windows 工程源码。
2. 人类把该 patch 保存为一个明确的 `.patch` 文件，在仓库根目录执行登记：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File agent_infra\patch_channel\New-MCUForgePatchProposal.ps1 `
  -PatchFile C:\明确路径\proposal.patch `
  -ProposalId FS-001-001
```

3. 首次可追加 `-DryRun`，它会完成全部校验但不创建任何提案文件。正式登记时，脚本会要求跟踪工作树干净，校验补丁格式、白名单、基础源码哈希和 `git apply --check`，然后在对应 Demo 的 `agent_profile/patch_proposals/FS-001-001/` 写入不可覆盖的 `proposal.patch` 与 `proposal.json`。该运行产物被 Git 忽略，但保留在本机供审计。
4. 人类先审阅这两个文件；只有确定要把补丁加入 Git 暂存区时，才从 `proposal.json` 读取 SHA-256 并执行：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File agent_infra\patch_channel\Apply-MCUForgeApprovedPatch.ps1 `
  -ProposalDirectory demos\um10550-board-demo\agent_profile\patch_proposals\FS-001-001 `
  -ApprovalToken "APPLY FS-001-001 <proposal.json 内 patch.sha256>"
```

5. 该脚本会再次校验策略、Git HEAD、补丁哈希和两份源码哈希；成功后仅执行 `git apply --index`，并留下 `apply-record.json`。接着由 Verification Agent 执行固定测试完整性检查与真实 Keil 构建；人类审阅通过后再正常 `git commit`。

## 停止条件

任一情况都拒绝应用：工作树有跟踪改动、当前 HEAD 改变、源码基线哈希不一致、补丁超出 64 KiB、补丁路径不在白名单、审批令牌不精确匹配。拒绝时不会修改源码或 Git 暂存区。
