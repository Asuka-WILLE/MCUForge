param(
    [switch]$Rebuild,
    [string]$KeilPath = "C:\Users\hz_wu\AppData\Local\Keil_v5\UV4\UV4.exe",
    [string]$Target = "VCW",
    [string]$ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\..\demos\vcw-board-demo\firmware"))
)

$ErrorActionPreference = "Stop"
$projectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
$projectPath = Join-Path $projectRoot "MDK-ARM\VCW.uvprojx"
$buildDir = Join-Path $projectRoot "MDK-ARM\VCW"
$logPath = Join-Path $buildDir "VCW.build_log.htm"
$hexPath = Join-Path $buildDir "VCW.hex"
$axfPath = Join-Path $buildDir "VCW.axf"

if (-not (Test-Path -LiteralPath $KeilPath -PathType Leaf)) {
    throw "Keil executable not found: $KeilPath"
}
if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
    throw "Keil project not found: $projectPath"
}

$mode = if ($Rebuild) { "-r" } else { "-b" }
$process = Start-Process -FilePath $KeilPath `
    -ArgumentList @($mode, $projectPath, "-t", $Target) `
    -WorkingDirectory $projectRoot -WindowStyle Hidden -Wait -PassThru

$logText = if (Test-Path -LiteralPath $logPath -PathType Leaf) {
    Get-Content -LiteralPath $logPath -Raw
} else {
    ""
}
$summaryMatch = [regex]::Match($logText, '(\d+) Error\(s\),\s*(\d+) Warning\(s\)')
$sizeMatch = [regex]::Match($logText, 'Program Size:\s*Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)')

$result = [ordered]@{
    target = $Target
    mode = if ($Rebuild) { "rebuild" } else { "build" }
    process_exit_code = $process.ExitCode
    error_count = if ($summaryMatch.Success) { [int]$summaryMatch.Groups[1].Value } else { $null }
    warning_count = if ($summaryMatch.Success) { [int]$summaryMatch.Groups[2].Value } else { $null }
    program_size = if ($sizeMatch.Success) {
        [ordered]@{
            code = [int]$sizeMatch.Groups[1].Value
            ro_data = [int]$sizeMatch.Groups[2].Value
            rw_data = [int]$sizeMatch.Groups[3].Value
            zi_data = [int]$sizeMatch.Groups[4].Value
        }
    } else { $null }
    hex_path = $hexPath
    hex_sha256 = if (Test-Path -LiteralPath $hexPath -PathType Leaf) { (Get-FileHash -LiteralPath $hexPath -Algorithm SHA256).Hash } else { $null }
    axf_path = $axfPath
    axf_sha256 = if (Test-Path -LiteralPath $axfPath -PathType Leaf) { (Get-FileHash -LiteralPath $axfPath -Algorithm SHA256).Hash } else { $null }
    log_path = $logPath
}

$result | ConvertTo-Json -Depth 4
if ($process.ExitCode -ne 0 -or -not $summaryMatch.Success -or
    $result.error_count -ne 0 -or $result.warning_count -ne 0 -or
    -not $result.hex_sha256) {
    exit 1
}
