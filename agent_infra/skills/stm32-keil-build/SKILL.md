---
name: stm32-keil-build
description: Perform and audit a real Keil uVision build for this STM32 project, including exit code, error and warning counts, program size, and SHA-256 hashes. Use whenever an Agent changes firmware, the Keil project, USB code, or claims the STM32 build is ready.
compatibility: PowerShell 7, Keil uVision 5, UM10550 target
---

# STM32 Keil Build

Use the bundled `scripts/Invoke-KeilBuild.ps1` instead of inferring build success from source inspection.

## Workflow

1. Inspect `git status --short` and keep unrelated user changes intact.
2. Run a full rebuild after firmware changes:

   ```powershell
   pwsh -NoProfile -ExecutionPolicy Bypass -File agent_infra\skills\stm32-keil-build\scripts\Invoke-KeilBuild.ps1 -Rebuild
   ```

3. Treat success as all of the following: process exit code 0, Keil log reports 0 errors and 0 warnings, HEX exists, SHA-256 is present.
4. Report the exact target, program size, HEX path and hash. Do not claim the board was tested; build and hardware runtime are different evidence levels.
5. Never flash as part of this Skill. Provide the HEX hash to the user at the separate approval point.

## Output

The script prints one JSON object. Preserve it in the evidence record rather than paraphrasing away error counts or hashes.
