param(
    [string]$Port,
    [string]$Case = "FS-001",
    [switch]$List
)

$ErrorActionPreference = "Stop"
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\.."))
$integrityScript = Join-Path $repoRoot "agent_infra\skills\stm32-evidence-audit\scripts\Test-TestcaseIntegrity.ps1"
$runner = Join-Path $repoRoot "PC_Tools\mcuforge_test_runner.py"

& $integrityScript
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ($List) {
    & python $runner --list
    exit $LASTEXITCODE
}
if ([string]::IsNullOrWhiteSpace($Port)) {
    throw "-Port is required unless -List is used"
}

& python $runner --port $Port --case $Case
exit $LASTEXITCODE
