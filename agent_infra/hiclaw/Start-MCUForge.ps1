param(
    [string]$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..")),
    [string]$HiClawEnvPath = (Join-Path $env:USERPROFILE "hiclaw-manager.env"),
    [string]$Controller = "hiclaw-controller",
    [switch]$SkipDependencyBuild,
    [switch]$NoBrowser
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$resolvedRepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$logDirectory = Join-Path $env:LOCALAPPDATA "MCUForge"
$logPath = Join-Path $logDirectory "hiclaw-startup.log"

if (-not (Test-Path -LiteralPath $resolvedRepoRoot -PathType Container)) {
    throw "MCUForge 仓库目录不存在：$resolvedRepoRoot"
}

[void][System.IO.Directory]::CreateDirectory($logDirectory)

function Write-Status {
    param([string]$Message)

    $line = "[{0}] {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $Message
    Write-Host $line
    [System.IO.File]::AppendAllText($logPath, $line + [Environment]::NewLine, [System.Text.UTF8Encoding]::new($false))
}

function Test-DockerEngine {
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        return $false
    }

    & docker info 1>$null 2>$null
    return $LASTEXITCODE -eq 0
}

function Wait-Until {
    param(
        [scriptblock]$Condition,
        [int]$TimeoutSeconds,
        [string]$Description
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        if (& $Condition) {
            return
        }
        Start-Sleep -Seconds 2
    } while ((Get-Date) -lt $deadline)

    throw "等待超时：$Description"
}

function Start-DockerDesktop {
    $candidates = @(
        "C:\Program Files\Docker\Docker\Docker Desktop.exe",
        "C:\Program Files (x86)\Docker\Docker\Docker Desktop.exe",
        (Join-Path $env:LOCALAPPDATA "Docker\Docker Desktop.exe")
    )

    $desktopPath = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $desktopPath) {
        throw "Docker Desktop 未运行，且找不到 Docker Desktop.exe；请先手动启动 Docker Desktop。"
    }

    Write-Status "正在启动 Docker Desktop。"
    Start-Process -FilePath $desktopPath | Out-Null
}

function Start-HiClawContainers {
    $containerIds = @(docker ps -aq --filter "name=hiclaw-" 2>$null)
    if ($containerIds.Count -eq 0) {
        throw "找不到 HiClaw 容器；请先完成 HiClaw 安装。"
    }

    foreach ($containerId in $containerIds) {
        $id = "$containerId".Trim()
        if ($id) {
            $running = (docker inspect -f '{{.State.Running}}' $id 2>$null | Out-String).Trim()
            if ($running -ne "true") {
                Write-Status "正在启动 HiClaw 容器 $id。"
                docker start $id | Out-Null
            }
        }
    }

    Wait-Until -Condition {
        $state = (docker inspect -f '{{.State.Running}}' $Controller 2>$null | Out-String).Trim()
        return $state -eq "true"
    } -TimeoutSeconds 60 -Description $Controller
}

function Test-BridgeHealth {
    param([int]$Port)

    try {
        $response = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/health" -UseBasicParsing -TimeoutSec 2
        return $response.StatusCode -eq 200
    }
    catch {
        return $false
    }
}

function Ensure-BridgeBuild {
    param([string]$BridgeDirectory)

    $distEntry = Join-Path $BridgeDirectory "dist\index.js"
    if ($SkipDependencyBuild -and -not (Test-Path -LiteralPath $distEntry -PathType Leaf)) {
        throw "$distEntry 不存在，不能使用 -SkipDependencyBuild。"
    }
    if (Test-Path -LiteralPath $distEntry -PathType Leaf) {
        return
    }
    if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
        throw "找不到 Node.js；请安装 Node.js 20 或更高版本。"
    }
    if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
        throw "找不到 npm；请检查 Node.js 安装。"
    }

    Write-Status "正在准备 Bridge 依赖：$BridgeDirectory。"
    Push-Location $BridgeDirectory
    try {
        if (-not (Test-Path -LiteralPath (Join-Path $BridgeDirectory "node_modules") -PathType Container)) {
            npm ci
            if ($LASTEXITCODE -ne 0) {
                throw "npm ci 失败：$BridgeDirectory"
            }
        }

        npm run build
        if ($LASTEXITCODE -ne 0) {
            throw "npm run build 失败：$BridgeDirectory"
        }
    }
    finally {
        Pop-Location
    }
}

function Start-Bridge {
    param(
        [string]$BridgeScript,
        [int]$Port,
        [string]$Label
    )

    if (Test-BridgeHealth -Port $Port) {
        Write-Status "$Label 已在运行，复用现有进程。"
        return
    }

    $pwsh = (Get-Command pwsh -ErrorAction SilentlyContinue).Source
    if (-not $pwsh) {
        throw "找不到 PowerShell 7（pwsh）；两个 Bridge 需要 PowerShell 7。"
    }

    Write-Status "正在启动 $Label。"
    $bridgeArguments = @(
        "-NoLogo",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        $BridgeScript
    )
    Start-Process -FilePath $pwsh -ArgumentList $bridgeArguments -WorkingDirectory $resolvedRepoRoot | Out-Null
    Wait-Until -Condition { Test-BridgeHealth -Port $Port } -TimeoutSeconds 60 -Description "$Label 健康检查"
}

function Invoke-PowerShellScript {
    param(
        [string]$ScriptPath,
        [string[]]$Arguments
    )

    $pwsh = (Get-Command pwsh -ErrorAction SilentlyContinue).Source
    if (-not $pwsh) {
        throw "找不到 PowerShell 7（pwsh）。"
    }

    & $pwsh -NoLogo -NoProfile -ExecutionPolicy Bypass -File $ScriptPath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "脚本执行失败（$LASTEXITCODE）：$ScriptPath"
    }
}

try {
    Write-Status "开始恢复 MCUForge HiClaw。"

    if (-not (Test-DockerEngine)) {
        Start-DockerDesktop
        Wait-Until -Condition { Test-DockerEngine } -TimeoutSeconds 120 -Description "Docker 引擎"
    }
    Write-Status "Docker 引擎已就绪。"

    Start-HiClawContainers
    Write-Status "HiClaw Controller 已就绪。"

    $stm32Bridge = Join-Path $resolvedRepoRoot "agent_infra\tool_bridge\stm32-mcp-server"
    $researchBridge = Join-Path $resolvedRepoRoot "agent_infra\tool_bridge\research-web-mcp-server"
    Ensure-BridgeBuild -BridgeDirectory $stm32Bridge
    Ensure-BridgeBuild -BridgeDirectory $researchBridge

    Start-Bridge -BridgeScript (Join-Path $stm32Bridge "Start-STM32ToolBridge.ps1") -Port 8765 -Label "STM32 Bridge"
    Start-Bridge -BridgeScript (Join-Path $researchBridge "Start-ResearchWebBridge.ps1") -Port 8766 -Label "Research Bridge"

    if (-not (Test-Path -LiteralPath $HiClawEnvPath -PathType Leaf)) {
        throw "找不到 HiClaw 环境文件：$HiClawEnvPath；请确认安装时生成了该文件。"
    }

    $configureStm32 = Join-Path $stm32Bridge "Configure-HiClawProxy.ps1"
    $configureResearch = Join-Path $researchBridge "Configure-HiClawResearchProxy.ps1"
    $bootstrap = Join-Path $resolvedRepoRoot "agent_infra\hiclaw\Bootstrap-MCUForgeTeam.ps1"

    Write-Status "正在注册两个 MCP Bridge。"
    Invoke-PowerShellScript -ScriptPath $configureStm32 -Arguments @(
        "-HiClawEnvPath", $HiClawEnvPath,
        "-Controller", $Controller,
        "-EnableWideAgentAccess"
    )
    Invoke-PowerShellScript -ScriptPath $configureResearch -Arguments @(
        "-HiClawEnvPath", $HiClawEnvPath,
        "-Controller", $Controller,
        "-EnableWideAgentAccess"
    )

    Write-Status "正在 Bootstrap mcuforge Team。"
    Invoke-PowerShellScript -ScriptPath $bootstrap -Arguments @(
        "-Controller", $Controller,
        "-EnableToolBridge",
        "-EnableResearchBridge",
        "-EnableWideAgentAccess"
    )

    Write-Status "验证 Team 与 Worker。"
    docker exec $Controller hiclaw get teams mcuforge
    if ($LASTEXITCODE -ne 0) {
        throw "无法读取 mcuforge Team 状态。"
    }
    docker exec $Controller hiclaw get workers --team mcuforge
    if ($LASTEXITCODE -ne 0) {
        throw "无法读取 mcuforge Worker 状态。"
    }

    if (-not $NoBrowser) {
        Write-Status "正在打开 HiClaw Element Web。"
        Start-Process "http://127.0.0.1:18088" | Out-Null
    }

    Write-Status "HiClaw 恢复完成；Manager 控制台：http://127.0.0.1:18888"
    Write-Status "启动日志：$logPath"
}
catch {
    Write-Status "启动失败：$($_.Exception.Message)"
    throw
}
