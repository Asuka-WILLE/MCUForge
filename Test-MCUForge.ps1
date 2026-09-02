[CmdletBinding()]
param(
    [ValidateSet("Static", "Live", "Full")]
    [string]$Mode = "Static",
    [string]$Controller = "hiclaw-controller",
    [int]$CoordinationRounds = 1,
    [switch]$SkipNodeBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = $PSScriptRoot
$failures = [System.Collections.Generic.List[string]]::new()

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Action
    )

    Write-Host "[验证] $Name"
    try {
        & $Action
        Write-Host "  通过" -ForegroundColor Green
    }
    catch {
        $failures.Add("$Name：$($_.Exception.Message)")
        Write-Host "  失败：$($_.Exception.Message)" -ForegroundColor Red
    }
}

Invoke-Step -Name "PowerShell 脚本语法" -Action {
    $roots = @(
        (Join-Path $repoRoot "agent_infra"),
        (Join-Path $repoRoot "demos\vcw-board-demo")
    )
    $scripts = @(
        Get-ChildItem -LiteralPath $repoRoot -Filter "*.ps1" -File
        Get-ChildItem -LiteralPath $roots[0] -Filter "*.ps1" -File -Recurse
        Get-ChildItem -LiteralPath $roots[1] -Filter "*.ps1" -File -Recurse
    ) | Sort-Object FullName -Unique

    foreach ($script in $scripts) {
        $tokens = $null
        $parseErrors = $null
        [void][System.Management.Automation.Language.Parser]::ParseFile(
            $script.FullName,
            [ref]$tokens,
            [ref]$parseErrors
        )
        if ($parseErrors.Count -gt 0) {
            throw "$($script.FullName)：$($parseErrors[0].Message)"
        }
    }
    Write-Host "  已解析 $($scripts.Count) 个脚本"
}

Invoke-Step -Name "Python Worker Control Bridge 语法" -Action {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $python) {
        throw "找不到 python"
    }
    $source = Join-Path $repoRoot "agent_infra\hiclaw\worker-control-bridge\worker_control_bridge.py"
    & $python.Source -c "import ast,pathlib,sys; p=pathlib.Path(sys.argv[1]); ast.parse(p.read_text(encoding='utf-8-sig'), filename=str(p))" $source
    if ($LASTEXITCODE -ne 0) {
        throw "Python AST 解析失败"
    }
}

$gitCommand = Get-Command git -ErrorAction SilentlyContinue
$gitBash = $null
if ($gitCommand) {
    $gitRoot = Split-Path -Parent (Split-Path -Parent $gitCommand.Source)
    $gitBash = @(
        (Join-Path $gitRoot "bin\bash.exe"),
        (Join-Path $gitRoot "usr\bin\bash.exe")
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if ($gitBash) {
    Invoke-Step -Name "项目房间 Shell 脚本语法" -Action {
        $script = Join-Path $repoRoot "agent_infra\hiclaw\manager-project-room\scripts\create-project.sh"
        & $gitBash -n $script
        if ($LASTEXITCODE -ne 0) {
            throw "bash -n 失败"
        }
    }
}
else {
    Write-Host "[跳过] 未找到 bash；项目房间脚本会在 HiClaw Linux 容器中执行。" -ForegroundColor Yellow
}

if (-not $SkipNodeBuild) {
    foreach ($relativePath in @(
        "agent_infra\tool_bridge\stm32-mcp-server",
        "agent_infra\tool_bridge\research-web-mcp-server"
    )) {
        Invoke-Step -Name "TypeScript 构建：$relativePath" -Action {
            $bridgePath = Join-Path $repoRoot $relativePath
            Push-Location $bridgePath
            try {
                if (-not (Test-Path -LiteralPath (Join-Path $bridgePath "node_modules") -PathType Container)) {
                    & npm ci
                    if ($LASTEXITCODE -ne 0) {
                        throw "npm ci 失败"
                    }
                }
                & npm run build
                if ($LASTEXITCODE -ne 0) {
                    throw "npm run build 失败"
                }
            }
            finally {
                Pop-Location
            }
        }
    }
}

Invoke-Step -Name "Git 空白与冲突标记检查" -Action {
    & git -C $repoRoot diff --check
    if ($LASTEXITCODE -ne 0) {
        throw "git diff --check 未通过"
    }
}

if ($Mode -in @("Live", "Full")) {
    foreach ($port in @(8765, 8766)) {
        Invoke-Step -Name "Bridge 健康检查：$port" -Action {
            $response = Invoke-RestMethod -Uri "http://127.0.0.1:$port/health" -TimeoutSec 5
            if (-not $response -or ($response.PSObject.Properties.Name -contains "ok" -and $response.ok -ne $true)) {
                throw "健康端点未返回 ok=true"
            }
        }
    }

    Invoke-Step -Name "HiClaw Team 状态" -Action {
        $teamJson = & docker exec $Controller hiclaw get teams mcuforge -o json 2>$null | Out-String
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($teamJson)) {
            throw "无法读取 Team"
        }
        $team = $teamJson | ConvertFrom-Json
        if ($team.phase -ne "Active" -or $team.leaderReady -ne $true -or [int]$team.readyWorkers -ne 4) {
            throw "Team 未达到 Active + Leader ready + 4/4 Workers"
        }
    }
}

if ($Mode -eq "Full") {
    Invoke-Step -Name "Manager/Worker 真实协调（$CoordinationRounds 轮）" -Action {
        $script = Join-Path $repoRoot "agent_infra\hiclaw\Test-MCUForgeCoordination.ps1"
        & $script -Controller $Controller -Rounds $CoordinationRounds
        if ($LASTEXITCODE -ne 0) {
            throw "协调验收失败"
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "`n验证失败，共 $($failures.Count) 项：" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "- $_" }
    exit 1
}

Write-Host "`nMCUForge $Mode 验证通过。" -ForegroundColor Green
