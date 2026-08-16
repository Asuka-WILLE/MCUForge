param(
    [string]$Port,
    [string]$Case = "FS-001",
    [switch]$List,
    [string]$ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\..\demos\vcw-board-demo\firmware")),
    [string]$ProfileRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\..\demos\vcw-board-demo\agent_profile"))
)

$ErrorActionPreference = "Stop"
$agentRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\.."))
$projectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
$profileRoot = [System.IO.Path]::GetFullPath($ProfileRoot)
$integrityScript = Join-Path $agentRoot "skills\stm32-evidence-audit\scripts\Test-TestcaseIntegrity.ps1"
$runner = Join-Path $projectRoot "PC_Tools\mcuforge_test_runner.py"

& $integrityScript -ProjectRoot $projectRoot -ProfileRoot $profileRoot
if (-not $?) {
    exit 1
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
