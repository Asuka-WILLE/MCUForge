---
name: stm32-evidence-audit
description: Audit STM32 Agent work for scope violations, immutable-test tampering, missing Keil or hardware evidence, and unsafe claims. Use before accepting any firmware change, AgentTeams handoff, demo milestone, local tag, push, or statement that the task is complete.
compatibility: Git, PowerShell 7
---

# STM32 Evidence Audit

## Workflow

1. Run `scripts/Test-TestcaseIntegrity.ps1`; reject any missing, extra or hash-mismatched fixed test.
2. Inspect `git status --short` and `git diff --check` before reading conclusions.
3. Compare modified paths with the active Demo Profile's `roles/` and `patch-policy.json`.
4. Require separate evidence levels:
   - static: Python compile/unit tests;
   - build: real Keil exit code, error/warning counts and HEX hash;
   - hardware: approved firmware, actual COM port, raw telemetry and fixed assertions;
   - visual: TFT photo tied to the tested firmware/session.
5. Reject these invalid shortcuts: static inspection called a build, build called a hardware pass, changed tests called a fix, or an unapproved flash/push.

## Verdict format

Return `PASS`, `REJECT`, or `WAITING_FOR_USER_APPROVAL`, followed by exact evidence paths, failed checks, out-of-scope files, and the next authorized action.
