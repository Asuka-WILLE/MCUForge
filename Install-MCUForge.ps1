[CmdletBinding()]
param(
    [string]$HiClawEnvPath = (Join-Path $env:USERPROFILE "hiclaw-manager.env"),
    [string]$Controller = "hiclaw-controller",
    [switch]$SkipEnvironmentCheck
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = $PSScriptRoot

if (-not $SkipEnvironmentCheck) {
    & (Join-Path $repoRoot "Check-MCUForgeEnvironment.ps1") `
        -HiClawEnvPath $HiClawEnvPath -Controller $Controller
    if ($LASTEXITCODE -ne 0) {
        throw "环境检查未通过，安装已停止。"
    }
}

$bridges = @(
    "agent_infra\tool_bridge\stm32-mcp-server",
    "agent_infra\tool_bridge\research-web-mcp-server"
)

foreach ($relativePath in $bridges) {
    $bridgePath = Join-Path $repoRoot $relativePath
    Write-Host "[安装] 正在安装并锁定依赖：$relativePath"
    Push-Location $bridgePath
    try {
        & npm ci
        if ($LASTEXITCODE -ne 0) {
            throw "npm ci 失败：$relativePath"
        }

        & npm run build
        if ($LASTEXITCODE -ne 0) {
            throw "npm run build 失败：$relativePath"
        }
    }
    finally {
        Pop-Location
    }
}

Write-Host "`n安装完成。下一步运行：" -ForegroundColor Green
Write-Host "pwsh -NoProfile -ExecutionPolicy Bypass -File .\Start-MCUForge.ps1"
