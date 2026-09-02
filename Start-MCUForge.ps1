[CmdletBinding()]
param(
    [string]$HiClawEnvPath = (Join-Path $env:USERPROFILE "hiclaw-manager.env"),
    [string]$Controller = "hiclaw-controller",
    [switch]$SkipDependencyBuild,
    [switch]$ForceTeamBootstrap,
    [switch]$NoBrowser
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$entry = Join-Path $PSScriptRoot "agent_infra\hiclaw\Start-MCUForge.ps1"
if (-not (Test-Path -LiteralPath $entry -PathType Leaf)) {
    throw "找不到内部启动脚本：$entry；请重新克隆完整仓库。"
}

& $entry -RepoRoot $PSScriptRoot -HiClawEnvPath $HiClawEnvPath -Controller $Controller `
    -SkipDependencyBuild:$SkipDependencyBuild -ForceTeamBootstrap:$ForceTeamBootstrap -NoBrowser:$NoBrowser

if ($LASTEXITCODE -ne 0) {
    throw "MCUForge 启动失败，退出码：$LASTEXITCODE"
}
