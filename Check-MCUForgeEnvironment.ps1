[CmdletBinding()]
param(
    [string]$HiClawEnvPath = (Join-Path $env:USERPROFILE "hiclaw-manager.env"),
    [string]$Controller = "hiclaw-controller"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = $PSScriptRoot
$checks = [System.Collections.Generic.List[object]]::new()

function Add-Check {
    param(
        [string]$Name,
        [bool]$Required,
        [bool]$Passed,
        [string]$Details,
        [string]$Fix
    )

    $checks.Add([pscustomobject]@{
        Item = $Name
        Required = if ($Required) { "是" } else { "否" }
        Status = if ($Passed) { "通过" } else { "失败" }
        Details = $Details
        Fix = if ($Passed) { "-" } else { $Fix }
    })
}

function Test-HttpHealth {
    param([int]$Port)

    try {
        $response = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/health" -UseBasicParsing -TimeoutSec 2
        return $response.StatusCode -eq 200
    }
    catch {
        return $false
    }
}

$isPowerShell7 = $PSVersionTable.PSVersion.Major -ge 7
Add-Check -Name "PowerShell 7" -Required $true -Passed $isPowerShell7 `
    -Details $PSVersionTable.PSVersion.ToString() -Fix "安装 PowerShell 7，并使用 pwsh 执行脚本。"

$requiredCommands = @(
    @{ Name = "Git"; Command = "git"; Fix = "安装 Git for Windows。" },
    @{ Name = "Docker"; Command = "docker"; Fix = "安装并启动 Docker Desktop。" },
    @{ Name = "Node.js"; Command = "node"; Fix = "安装 Node.js 20 或更高版本。" },
    @{ Name = "npm"; Command = "npm"; Fix = "修复 Node.js/npm 安装。" }
)

foreach ($entry in $requiredCommands) {
    $command = Get-Command $entry.Command -ErrorAction SilentlyContinue
    Add-Check -Name $entry.Name -Required $true -Passed ($null -ne $command) `
        -Details $(if ($command) { $command.Source } else { "未找到" }) -Fix $entry.Fix
}

$nodeCommand = Get-Command node -ErrorAction SilentlyContinue
if ($nodeCommand) {
    $nodeVersionText = (& node --version 2>$null | Out-String).Trim()
    $nodeMajor = 0
    if ($nodeVersionText -match '^v([0-9]+)') {
        $nodeMajor = [int]$Matches[1]
    }
    Add-Check -Name "Node.js 版本" -Required $true -Passed ($nodeMajor -ge 20) `
        -Details $nodeVersionText -Fix "升级到 Node.js 20 或更高版本。"
}

$dockerCommand = Get-Command docker -ErrorAction SilentlyContinue
$dockerReady = $false
if ($dockerCommand) {
    & docker info 1>$null 2>$null
    $dockerReady = $LASTEXITCODE -eq 0
}
Add-Check -Name "Docker 引擎" -Required $true -Passed $dockerReady `
    -Details $(if ($dockerReady) { "可访问" } else { "不可访问" }) -Fix "启动 Docker Desktop，等待状态显示 Running。"

$controllerReady = $false
if ($dockerReady) {
    $controllerState = (& docker inspect -f '{{.State.Running}}' $Controller 2>$null | Out-String).Trim()
    $controllerReady = $controllerState -eq "true"
}
Add-Check -Name "HiClaw Controller" -Required $true -Passed $controllerReady `
    -Details $(if ($controllerReady) { "$Controller 正在运行" } else { "$Controller 未运行或不存在" }) `
    -Fix "先按 HiClaw 官方文档完成部署，或传入正确的 -Controller。"

$workerImage = "local/mcuforge-hiclaw-worker:policy-safe-20260902-v2"
$workerControlImage = "local/mcuforge-worker-control:20260902"
$imageFix = "运行 agent_infra\hiclaw\worker-image-policy-safe\Build-MCUForgeWorkerImage.ps1 自动构建；或从交付包执行 docker load -i .\mcuforge-hiclaw-images.tar.gz 导入。"
if ($dockerReady) {
    & docker image inspect $workerImage 1>$null 2>$null
    $foundWorker = $LASTEXITCODE -eq 0
    & docker image inspect $workerControlImage 1>$null 2>$null
    $foundControl = $LASTEXITCODE -eq 0
    Add-Check -Name "MCUForge Worker 镜像" -Required $true -Passed $foundWorker `
        -Details $(if ($foundWorker) { $workerImage } else { "缺失：$workerImage" }) -Fix $imageFix
    Add-Check -Name "Worker Control 镜像" -Required $true -Passed $foundControl `
        -Details $(if ($foundControl) { $workerControlImage } else { "缺失：$workerControlImage" }) -Fix $imageFix
}
else {
    Add-Check -Name "MCUForge Worker 镜像" -Required $true -Passed $false `
        -Details "Docker 引擎不可用，无法检查 $workerImage" -Fix "先启动 Docker Desktop。"
    Add-Check -Name "Worker Control 镜像" -Required $true -Passed $false `
        -Details "Docker 引擎不可用，无法检查 $workerControlImage" -Fix "先启动 Docker Desktop。"
}

$envExists = Test-Path -LiteralPath $HiClawEnvPath -PathType Leaf
Add-Check -Name "HiClaw 环境文件" -Required $true -Passed $envExists `
    -Details $HiClawEnvPath -Fix "找到自己安装时生成的 hiclaw-manager.env，并用 -HiClawEnvPath 指定；不要复制别人的密钥。"

$requiredPaths = @(
    "agent_infra\hiclaw\Start-MCUForge.ps1",
    "agent_infra\hiclaw\Bootstrap-MCUForgeTeam.ps1",
    "agent_infra\tool_bridge\stm32-mcp-server\package-lock.json",
    "agent_infra\tool_bridge\research-web-mcp-server\package-lock.json",
    "demos\vcw-board-demo\firmware",
    "demos\vcw-board-demo\agent_profile"
)
$missingPaths = @($requiredPaths | Where-Object { -not (Test-Path -LiteralPath (Join-Path $repoRoot $_)) })
Add-Check -Name "代码包完整性" -Required $true -Passed ($missingPaths.Count -eq 0) `
    -Details $(if ($missingPaths.Count -eq 0) { "关键目录齐全" } else { "缺少：$($missingPaths -join ', ')" }) `
    -Fix "重新克隆 GitHub 仓库，不要只复制单个脚本。"

Add-Check -Name "STM32 Bridge（运行态）" -Required $false -Passed (Test-HttpHealth -Port 8765) `
    -Details "http://127.0.0.1:8765/health" -Fix "安装后运行 .\Start-MCUForge.ps1。"
Add-Check -Name "Research Bridge（运行态）" -Required $false -Passed (Test-HttpHealth -Port 8766) `
    -Details "http://127.0.0.1:8766/health" -Fix "安装后运行 .\Start-MCUForge.ps1。"

$keilCandidates = @(
    "C:\Keil_v5\UV4\UV4.exe",
    "C:\Keil\UV4\UV4.exe"
)
$keilPath = $keilCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
Add-Check -Name "Keil uVision 5（真实构建）" -Required $false -Passed ($null -ne $keilPath) `
    -Details $(if ($keilPath) { $keilPath } else { "未在默认路径发现" }) `
    -Fix "需要真实固件构建时再安装 Keil uVision 5。"

$checks | Format-Table -AutoSize -Wrap

$requiredFailures = @($checks | Where-Object { $_.Required -eq "是" -and $_.Status -eq "失败" })
if ($requiredFailures.Count -gt 0) {
    Write-Host "`n环境检查失败：$($requiredFailures.Count) 个必需项未通过。" -ForegroundColor Red
    exit 1
}

Write-Host "`n环境检查通过。可以运行 .\Install-MCUForge.ps1。" -ForegroundColor Green
