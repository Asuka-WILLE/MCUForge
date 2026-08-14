$ErrorActionPreference = "Stop"
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\.."))
$lockPath = Join-Path $repoRoot "agent_infra\testcase-lock.json"
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
$caseRoot = Join-Path $repoRoot ($lock.root -replace '/', '\')
$failures = @()

foreach ($property in $lock.files.PSObject.Properties) {
    $casePath = Join-Path $caseRoot $property.Name
    if (-not (Test-Path -LiteralPath $casePath -PathType Leaf)) {
        $failures += "missing: $($property.Name)"
        continue
    }
    $actual = (Get-FileHash -LiteralPath $casePath -Algorithm SHA256).Hash
    if ($actual -ne $property.Value) {
        $failures += "hash mismatch: $($property.Name) expected=$($property.Value) actual=$actual"
    }
}

$knownFiles = @($lock.files.PSObject.Properties.Name)
$extraFiles = Get-ChildItem -LiteralPath $caseRoot -File -Filter "*.json" |
    Where-Object { $_.Name -notin $knownFiles }
foreach ($extra in $extraFiles) {
    $failures += "unexpected testcase: $($extra.Name)"
}

if ($failures.Count -gt 0) {
    [ordered]@{ passed = $false; failures = $failures } | ConvertTo-Json -Depth 3
    exit 1
}

[ordered]@{ passed = $true; checked_files = $knownFiles } | ConvertTo-Json -Depth 3
