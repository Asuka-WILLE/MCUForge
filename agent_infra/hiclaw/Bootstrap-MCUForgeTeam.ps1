param(
    [string]$Controller = "hiclaw-controller",
    [string]$Model = "deepseek-v4-pro",
    [string]$TeamName = "mcuforge",
    [switch]$EnableToolBridge
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$rolesRoot = Join-Path $scriptRoot "roles"

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw "docker command not found"
}

$controllerState = docker inspect -f '{{.State.Running}}' $Controller 2>$null
if ($LASTEXITCODE -ne 0 -or $controllerState -ne "true") {
    throw "HiClaw controller is not running: $Controller"
}

function Invoke-HiClaw {
    param([string[]]$Arguments)
    & docker exec $Controller hiclaw @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "hiclaw command failed: $($Arguments -join ' ')"
    }
}

function Get-RoleSoul {
    param([string]$RoleName)
    $source = Join-Path $rolesRoot "$RoleName\SOUL.md"
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "SOUL file not found: $source"
    }
    return Get-Content -LiteralPath $source -Raw
}

function ConvertTo-YamlBlock {
    param(
        [string]$Text,
        [int]$Indent
    )
    $prefix = " " * $Indent
    return (($Text.TrimEnd() -split "`r?`n") | ForEach-Object { "$prefix$_" }) -join "`n"
}

$workerDefinitions = @(
    @{ Name = "mcuforge-requirements"; RoleDirectory = "requirements"; Identity = "MCU requirements and architecture specialist"; ToolBridge = $false },
    @{ Name = "mcuforge-research"; RoleDirectory = "research"; Identity = "MCU manual, example and license research specialist"; ToolBridge = $false },
    @{ Name = "mcuforge-firmware"; RoleDirectory = "firmware"; Identity = "Modular embedded firmware engineer"; ToolBridge = $true },
    @{ Name = "mcuforge-verification"; RoleDirectory = "verification"; Identity = "Independent build, hardware test and evidence verifier"; ToolBridge = $true }
)

$leaderProtocol = ConvertTo-YamlBlock -Text (Get-RoleSoul -RoleName "leader") -Indent 6
$workerYaml = foreach ($worker in $workerDefinitions) {
    $roleSoul = ConvertTo-YamlBlock -Text (Get-RoleSoul -RoleName $worker.RoleDirectory) -Indent 8
    $toolBridgeYaml = if ($EnableToolBridge -and $worker.ToolBridge) {
        @"
      mcpServers:
        - name: stm32-tool-bridge
          url: http://aigw-local.hiclaw.io:8080/mcp-servers/mcp-stm32-tool-bridge/mcp
          transport: http
"@
    } else {
        ""
    }
    @"
    - name: $($worker.Name)
      runtime: openclaw
      model: $Model
      identity: $($worker.Identity)
$toolBridgeYaml
      soul: |
$roleSoul
"@
}

$teamManifest = @"
apiVersion: hiclaw.io/v1beta1
kind: Team
metadata:
  name: $TeamName
spec:
  description: MCU project intake, research, firmware implementation, independent verification and evidence
  peerMentions: true
  leader:
    name: mcuforge-lead
    model: $Model
    heartbeat:
      enabled: true
      every: 30m
    workerIdleTimeout: 12h
    agents: |
$leaderProtocol
  workers:
$($workerYaml -join "`n")
"@

$temporaryManifest = New-TemporaryFile
$controllerManifest = "/tmp/mcuforge-team.yaml"
try {
    [System.IO.File]::WriteAllText(
        $temporaryManifest.FullName,
        $teamManifest,
        [System.Text.UTF8Encoding]::new($false)
    )
    & docker cp $temporaryManifest.FullName "${Controller}:$controllerManifest"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to copy Team manifest into controller"
    }
    Invoke-HiClaw -Arguments @("apply", "-f", $controllerManifest)
}
finally {
    $temporaryRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    $resolvedManifest = [System.IO.Path]::GetFullPath($temporaryManifest.FullName)
    if ($resolvedManifest.StartsWith($temporaryRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedManifest -Force -ErrorAction SilentlyContinue
    }
}

# The first Matrix room join can race reconciliation on a fresh install.
# Re-applying harmless metadata retries reconciliation without recreating members.
Invoke-HiClaw -Arguments @(
    "update", "team",
    "--name", $TeamName,
    "--description", "MCU project intake, research, firmware implementation, independent verification and evidence",
    "--leader-heartbeat-every", "30m",
    "--worker-idle-timeout", "12h"
)

$deadline = [DateTime]::UtcNow.AddMinutes(3)
$teamReady = $false
do {
    $teamState = Invoke-HiClaw -Arguments @("get", "teams", $TeamName, "-o", "json") |
        Out-String |
        ConvertFrom-Json
    if (
        $teamState.phase -eq "Active" -and
        $teamState.leaderReady -eq $true -and
        [int]$teamState.readyWorkers -eq $workerDefinitions.Count
    ) {
        $teamReady = $true
        break
    }
    if ($teamState.phase -eq "Failed") {
        Invoke-HiClaw -Arguments @(
            "update", "team",
            "--name", $TeamName,
            "--description", "MCU project intake, research, firmware implementation, independent verification and evidence"
        ) | Out-Null
    }
    Start-Sleep -Seconds 5
} while ([DateTime]::UtcNow -lt $deadline)

if (-not $teamReady) {
    throw "Team did not become Active with all workers ready within 3 minutes"
}

Invoke-HiClaw -Arguments @("status")
Invoke-HiClaw -Arguments @("get", "teams", $TeamName)
Invoke-HiClaw -Arguments @("get", "workers", "--team", $TeamName)
