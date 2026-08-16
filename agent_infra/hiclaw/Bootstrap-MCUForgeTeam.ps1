param(
    [string]$Controller = "hiclaw-controller",
    [string]$Model = "deepseek-v4-pro",
    [string]$TeamName = "mcuforge",
    [string]$ProfileRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\demos\um10550-board-demo\agent_profile")),
    [switch]$EnableToolBridge,
    [switch]$EnableResearchBridge,
    [switch]$SkipBundledSkills
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$rolesRoot = Join-Path ([System.IO.Path]::GetFullPath($ProfileRoot)) "hiclaw_roles"

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
    @{ Name = "mcuforge-requirements"; RoleDirectory = "requirements"; Identity = "MCU requirements and architecture specialist"; ToolBridge = $false; ResearchBridge = $false },
    @{ Name = "mcuforge-research"; RoleDirectory = "research"; Identity = "MCU manual, example and license research specialist"; ToolBridge = $false; ResearchBridge = $true },
    @{ Name = "mcuforge-firmware"; RoleDirectory = "firmware"; Identity = "Modular embedded firmware engineer"; ToolBridge = $true; ResearchBridge = $false },
    @{ Name = "mcuforge-verification"; RoleDirectory = "verification"; Identity = "Independent build, hardware test and evidence verifier"; ToolBridge = $true; ResearchBridge = $false }
)

$leaderProtocol = ConvertTo-YamlBlock -Text (Get-RoleSoul -RoleName "leader") -Indent 6
$workerYaml = foreach ($worker in $workerDefinitions) {
    $roleSoul = ConvertTo-YamlBlock -Text (Get-RoleSoul -RoleName $worker.RoleDirectory) -Indent 8
    $mcpEntries = [System.Collections.Generic.List[string]]::new()
    if ($EnableToolBridge -and $worker.ToolBridge) {
        [void]$mcpEntries.Add(@"
        - name: stm32-tool-bridge
          url: http://aigw-local.hiclaw.io:8080/mcp-servers/mcp-stm32-tool-bridge/mcp
          transport: http
"@)
    }
    if ($EnableResearchBridge -and $worker.ResearchBridge) {
        [void]$mcpEntries.Add(@"
        - name: research-web-bridge
          url: http://aigw-local.hiclaw.io:8080/mcp-servers/mcp-research-web-bridge/mcp
          transport: http
"@)
    }
    $mcpServersYaml = if ($mcpEntries.Count -gt 0) { "      mcpServers:`n" + ($mcpEntries -join "`n") } else { "" }
    @"
    - name: $($worker.Name)
      runtime: openclaw
      model: $Model
      identity: $($worker.Identity)
$mcpServersYaml
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

if (-not $SkipBundledSkills) {
    $skillInstaller = Join-Path $scriptRoot "Install-MCUForgeSkills.ps1"
    if (-not (Test-Path -LiteralPath $skillInstaller -PathType Leaf)) {
        throw "Bundled Skill installer not found: $skillInstaller"
    }
    & $skillInstaller -Controller $Controller
    if ($LASTEXITCODE -ne 0) {
        throw "Bundled Skill installation failed"
    }
}

# The controller stores the Team manifest, but an existing CoPaw Leader keeps
# its protocol file in its workspace.  Re-applying a Team therefore does not
# hot-reload an edited leader SOUL.  Synchronize the source-of-truth file to
# persistent storage and refresh the Leader only when its hash changed.
function Sync-LeaderProtocol {
    $leaderWorker = "hiclaw-worker-mcuforge-lead"
    $leaderSoulSource = Join-Path $rolesRoot "leader\SOUL.md"
    $leaderSoulHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $leaderSoulSource).Hash.ToLowerInvariant()
    $persistentSoulHash = (& docker exec $Controller sh -lc 'mc cat hiclaw/hiclaw-storage/agents/mcuforge-lead/SOUL.md 2>/dev/null | sha256sum | cut -d " " -f1' 2>$null | Out-String).Trim().ToLowerInvariant()
    $runtimeSoulHash = (& docker exec $leaderWorker sh -lc 'sha256sum /root/hiclaw-fs/agents/mcuforge-lead/SOUL.md 2>/dev/null | cut -d " " -f1' 2>$null | Out-String).Trim().ToLowerInvariant()

    if ($persistentSoulHash -eq $leaderSoulHash -and $runtimeSoulHash -eq $leaderSoulHash) {
        return
    }

    $temporarySoul = New-TemporaryFile
    try {
        Copy-Item -LiteralPath $leaderSoulSource -Destination $temporarySoul.FullName -Force
        if ($persistentSoulHash -ne $leaderSoulHash) {
            & docker cp $temporarySoul.FullName "${Controller}:/tmp/mcuforge-lead-SOUL.md"
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to copy the Leader protocol into the controller"
            }
            & docker exec $Controller sh -lc 'mc cp /tmp/mcuforge-lead-SOUL.md hiclaw/hiclaw-storage/agents/mcuforge-lead/SOUL.md'
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to persist the Leader protocol in HiClaw storage"
            }
        }
        if ($runtimeSoulHash -ne $leaderSoulHash) {
            & docker cp $temporarySoul.FullName "${leaderWorker}:/root/hiclaw-fs/agents/mcuforge-lead/SOUL.md"
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to synchronize the running Leader protocol"
            }
            & docker restart $leaderWorker | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to restart the Leader after protocol update"
            }

            $refreshDeadline = [DateTime]::UtcNow.AddMinutes(3)
            $leaderReadyAfterRefresh = $false
            do {
                $refreshedTeamState = Invoke-HiClaw -Arguments @("get", "teams", $TeamName, "-o", "json") |
                    Out-String |
                    ConvertFrom-Json
                if ($refreshedTeamState.phase -eq "Active" -and $refreshedTeamState.leaderReady -eq $true) {
                    $leaderReadyAfterRefresh = $true
                    break
                }
                Start-Sleep -Seconds 5
            } while ([DateTime]::UtcNow -lt $refreshDeadline)

            if (-not $leaderReadyAfterRefresh) {
                throw "Leader did not become ready after protocol refresh"
            }
        }
    }
    finally {
        $resolvedTemporarySoul = [System.IO.Path]::GetFullPath($temporarySoul.FullName)
        if (Test-Path -LiteralPath $resolvedTemporarySoul) {
            Remove-Item -LiteralPath $resolvedTemporarySoul -Force -ErrorAction SilentlyContinue
        }
    }
}

Sync-LeaderProtocol

Invoke-HiClaw -Arguments @("status")
Invoke-HiClaw -Arguments @("get", "teams", $TeamName)
Invoke-HiClaw -Arguments @("get", "workers", "--team", $TeamName)
