---
name: stm32-fixed-hardware-test
description: Run immutable MCUForge USB CDC black-box tests against the STM32 board and preserve raw telemetry plus machine-readable results. Use whenever the user asks to verify timeout, failsafe, recovery, e-stop, virtual control, COM3 behavior, or post-flash acceptance.
compatibility: Windows PowerShell, Python 3, pyserial, connected STM32 USB CDC
---

# STM32 Fixed Hardware Test

## Workflow

1. Confirm the user has already approved flashing and the expected HEX is on the board. This Skill does not flash.
2. Run testcase integrity first. Any mismatch is a hard stop because implementation Agents are not allowed to weaken tests.
3. List cases without touching the serial port:

   ```powershell
   powershell -ExecutionPolicy Bypass -File agent_infra\skills\stm32-fixed-hardware-test\scripts\Invoke-FixedHardwareTest.ps1 -List
   ```

4. Execute one case on the actual port, normally COM3:

   ```powershell
   powershell -ExecutionPolicy Bypass -File agent_infra\skills\stm32-fixed-hardware-test\scripts\Invoke-FixedHardwareTest.ps1 -Port COM3 -Case FS-001
   ```

5. Keep `result.json` and `telemetry.jsonl`. A failed assertion is valid evidence, not permission to edit the test.

## Reporting

Report case ID, pass/fail, assertion failures, sample count, evidence directory, firmware HEX hash, port, and test time. Do not generalize one passing case into full acceptance.
