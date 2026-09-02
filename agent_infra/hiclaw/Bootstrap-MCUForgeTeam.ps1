param(
    [string]$Controller = "hiclaw-controller",
    [string]$ManagerContainer = "hiclaw-manager",
    [string]$HiClawEnvPath = (Join-Path $env:USERPROFILE "hiclaw-manager.env"),
    [string]$Model = "deepseek-v4-flash",
    [string]$WorkerImage = "local/mcuforge-hiclaw-worker:policy-safe-20260902-v2",
    [string]$WorkerControlImage = "local/mcuforge-worker-control:20260902",
    [string]$WorkerControlBaseImage = "python:3.12-alpine",
    [string]$WorkerControlContainer = "mcuforge-worker-control",
    [string]$TeamName = "mcuforge",
    [string]$ProfileRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\demos\vcw-board-demo\agent_profile")),
    [switch]$EnableToolBridge,
    [switch]$EnableResearchBridge,
    [switch]$EnableWideAgentAccess,
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

function Ensure-WorkerControlBridge {
    $bridgeRoot = Join-Path $scriptRoot "worker-control-bridge"
    $dockerfile = Join-Path $bridgeRoot "Dockerfile"
    if (-not (Test-Path -LiteralPath $dockerfile -PathType Leaf)) {
        throw "Worker control bridge Dockerfile not found: $dockerfile"
    }
    $buildBaseImage = $WorkerControlBaseImage
    & docker image inspect $buildBaseImage *> $null
    if ($LASTEXITCODE -ne 0) {
        # Docker Desktop may temporarily lose its registry mirror.  If an
        # earlier bridge image exists, it already contains the same Python
        # runtime and is a safe offline build base for replacing only /app.
        & docker image inspect $WorkerControlImage *> $null
        if ($LASTEXITCODE -eq 0) {
            $buildBaseImage = "local/mcuforge-worker-control-buildbase:20260902"
            & docker tag $WorkerControlImage $buildBaseImage
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to prepare offline Worker control build base"
            }
        }
    }
    & docker build -q --build-arg "BASE_IMAGE=$buildBaseImage" -t $WorkerControlImage $bridgeRoot | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to build Worker control bridge: $WorkerControlImage"
    }
    $targetImageId = (& docker image inspect -f '{{.Id}}' $WorkerControlImage 2>$null | Out-String).Trim()
    if ([string]::IsNullOrWhiteSpace($targetImageId)) {
        throw "Worker control bridge image is not inspectable: $WorkerControlImage"
    }

    $containerImageId = (& docker inspect -f '{{.Image}}' $WorkerControlContainer 2>$null | Out-String).Trim()
    if (-not [string]::IsNullOrWhiteSpace($containerImageId) -and $containerImageId -ne $targetImageId) {
        # This removes one exact infrastructure helper container only. No Team,
        # Worker, room, volume, MinIO object or project data is removed.
        & docker rm -f $WorkerControlContainer | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to replace outdated Worker control bridge"
        }
        $containerImageId = ""
    }
    if ([string]::IsNullOrWhiteSpace($containerImageId)) {
        & docker run -d --name $WorkerControlContainer --restart unless-stopped --network hiclaw-net -v /var/run/docker.sock:/var/run/docker.sock $WorkerControlImage | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to start Worker control bridge"
        }
    }
    else {
        $running = (& docker inspect -f '{{.State.Running}}' $WorkerControlContainer 2>$null | Out-String).Trim()
        if ($running -ne "true") {
            & docker start $WorkerControlContainer | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to wake Worker control bridge"
            }
        }
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $health = (& docker exec $ManagerContainer curl -fsS http://${WorkerControlContainer}:18765/health 2>$null | Out-String).Trim()
        if ($LASTEXITCODE -eq 0 -and $health -match '"ok":true') {
            Write-Host "  Worker control bridge ready: $WorkerControlContainer"
            return
        }
        Start-Sleep -Seconds 2
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Worker control bridge did not become healthy within 30 seconds"
}

Ensure-WorkerControlBridge

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
    @{ Name = "mcuforge-requirements"; RoleDirectory = "requirements"; Identity = "MCU requirements and architecture specialist"; ToolBridge = $EnableWideAgentAccess; ResearchBridge = $EnableWideAgentAccess },
    @{ Name = "mcuforge-research"; RoleDirectory = "research"; Identity = "MCU manual, example and license research specialist"; ToolBridge = $EnableWideAgentAccess; ResearchBridge = $true },
    @{ Name = "mcuforge-firmware"; RoleDirectory = "firmware"; Identity = "Modular embedded firmware engineer"; ToolBridge = $true; ResearchBridge = $false },
    @{ Name = "mcuforge-verification"; RoleDirectory = "verification"; Identity = "Independent build, hardware test and evidence verifier"; ToolBridge = $true; ResearchBridge = $false }
)

$leaderProtocol = ConvertTo-YamlBlock -Text (Get-RoleSoul -RoleName "leader") -Indent 6
$leaderMcpYaml = if ($EnableToolBridge) {
    $leaderServers = [System.Collections.Generic.List[string]]::new()
    [void]$leaderServers.Add(@"
    mcpServers:
      - name: stm32-tool-bridge
        url: http://aigw-local.hiclaw.io:8080/mcp-servers/mcp-stm32-tool-bridge/mcp
        transport: http
"@)
    if ($EnableResearchBridge -and $EnableWideAgentAccess) {
        [void]$leaderServers.Add(@"
      - name: research-web-bridge
        url: http://aigw-local.hiclaw.io:8080/mcp-servers/mcp-research-web-bridge/mcp
        transport: http
"@)
    }
    ($leaderServers -join "`n")
} else { "" }
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
      image: $WorkerImage
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
      every: 5m
    workerIdleTimeout: 12h
$leaderMcpYaml
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
    & docker exec $ManagerContainer mkdir -p /root/manager-workspace/runtime
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to create Manager runtime directory"
    }
    & docker cp $temporaryManifest.FullName "${ManagerContainer}:/root/manager-workspace/runtime/$TeamName-team.yaml"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to persist Team manifest for Manager recovery"
    }
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
    "--leader-heartbeat-every", "5m",
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

# The controller may report the previous containers as ready while it is still
# rolling a new Worker image. Wait for the actual Docker containers to converge
# before copying protocols or issuing restarts, otherwise Bootstrap can race a
# controller replacement and target a container that has just stopped.
$imageDeadline = [DateTime]::UtcNow.AddMinutes(3)
$workerImagesReady = $false
do {
    $workerImagesReady = $true
    foreach ($worker in $workerDefinitions) {
        $container = "hiclaw-worker-$($worker.Name)"
        $actualImage = (& docker inspect -f '{{.Config.Image}}' $container 2>$null | Out-String).Trim()
        $running = (& docker inspect -f '{{.State.Running}}' $container 2>$null | Out-String).Trim()
        if ($actualImage -ne $WorkerImage -or $running -ne "true") {
            $workerImagesReady = $false
            break
        }
    }
    if (-not $workerImagesReady) { Start-Sleep -Seconds 3 }
} while (-not $workerImagesReady -and [DateTime]::UtcNow -lt $imageDeadline)

if (-not $workerImagesReady) {
    throw "Worker containers did not converge to image $WorkerImage within 3 minutes"
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

function Sync-LeaderMcpConfig {
    if (-not $EnableToolBridge) {
        return
    }

    # Older HiClaw embedded images do not project leader.mcpServers from the
    # Team manifest yet. Copy the already-authorized worker config so the
    # Leader receives the same gateway route without exposing a host token.
    $leaderWorker = "hiclaw-worker-mcuforge-lead"
    $sourceWorker = "hiclaw-worker-mcuforge-firmware"
    $containerPath = "/root/hiclaw-fs/agents/mcuforge-firmware/config/mcporter.json"
    $leaderConfigDirectory = "/root/hiclaw-fs/agents/mcuforge-lead/config"
    $leaderPath = "$leaderConfigDirectory/mcporter.json"
    $temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("mcuforge-leader-mcp-" + [Guid]::NewGuid().ToString("N"))
    [void](New-Item -ItemType Directory -Path $temporaryDirectory -Force)
    $temporaryConfig = Join-Path $temporaryDirectory "mcporter-servers.json"
    try {
        & docker cp "${sourceWorker}:$containerPath" $temporaryConfig | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to read the authorized STM32 MCP config from the Firmware worker"
        }
        $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $temporaryConfig).Hash.ToLowerInvariant()
        $leaderHash = (& docker exec $leaderWorker sh -lc "sha256sum $leaderPath 2>/dev/null | cut -d \" \" -f1" 2>$null | Out-String).Trim().ToLowerInvariant()
        if ($sourceHash -eq $leaderHash) {
            return
        }
        & docker exec $leaderWorker sh -lc "mkdir -p $leaderConfigDirectory" | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to create the Leader MCP config directory"
        }
        & docker cp $temporaryConfig "${leaderWorker}:$leaderPath" | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to synchronize the STM32 MCP config to the Leader"
        }
        & docker restart $leaderWorker | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to restart the Leader after MCP config update"
        }
        $refreshDeadline = [DateTime]::UtcNow.AddMinutes(3)
        do {
            $refreshedTeamState = Invoke-HiClaw -Arguments @("get", "teams", $TeamName, "-o", "json") |
                Out-String |
                ConvertFrom-Json
            if ($refreshedTeamState.phase -eq "Active" -and $refreshedTeamState.leaderReady -eq $true) {
                return
            }
            Start-Sleep -Seconds 5
        } while ([DateTime]::UtcNow -lt $refreshDeadline)
        throw "Leader did not become ready after MCP config refresh"
    }
    finally {
        $resolvedTemporaryConfig = [System.IO.Path]::GetFullPath($temporaryConfig)
        if (Test-Path -LiteralPath $resolvedTemporaryConfig -PathType Leaf) {
            Remove-Item -LiteralPath $resolvedTemporaryConfig -Force -ErrorAction SilentlyContinue
        }
        $resolvedTemporaryDirectory = [System.IO.Path]::GetFullPath($temporaryDirectory)
        if (Test-Path -LiteralPath $resolvedTemporaryDirectory -PathType Container) {
            Remove-Item -LiteralPath $resolvedTemporaryDirectory -Force -ErrorAction SilentlyContinue
        }
    }
}

Sync-LeaderMcpConfig

function Sync-WorkerProtocol {
    param([hashtable]$Worker)

    $workerName = $Worker.Name
    $workerSoulSource = Join-Path $rolesRoot "$($Worker.RoleDirectory)\SOUL.md"
    $workerSoulHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $workerSoulSource).Hash.ToLowerInvariant()
    $remoteObject = "hiclaw/hiclaw-storage/agents/$workerName/SOUL.md"
    $persistentSoulHash = (& docker exec $Controller sh -lc "mc cat $remoteObject 2>/dev/null | sha256sum | cut -d \" \" -f1" 2>$null | Out-String).Trim().ToLowerInvariant()
    $workerContainer = "hiclaw-worker-$workerName"
    $runtimePath = "/root/hiclaw-fs/agents/$workerName/SOUL.md"
    $runtimeSoulHash = (& docker exec $workerContainer sh -lc "sha256sum $runtimePath 2>/dev/null | cut -d \" \" -f1" 2>$null | Out-String).Trim().ToLowerInvariant()

    if ($persistentSoulHash -eq $workerSoulHash -and $runtimeSoulHash -eq $workerSoulHash) {
        return $false
    }

    $temporarySoul = New-TemporaryFile
    try {
        Copy-Item -LiteralPath $workerSoulSource -Destination $temporarySoul.FullName -Force
        if ($persistentSoulHash -ne $workerSoulHash) {
            $remoteTemp = "/tmp/$workerName-SOUL.md"
            & docker cp $temporarySoul.FullName "${Controller}:$remoteTemp" | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to copy the $workerName protocol into the controller"
            }
            & docker exec $Controller sh -lc "mc cp $remoteTemp $remoteObject" | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to persist the $workerName protocol in HiClaw storage"
            }
        }
        if ($runtimeSoulHash -ne $workerSoulHash) {
            & docker cp $temporarySoul.FullName "${workerContainer}:$runtimePath" | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to synchronize the running $workerName protocol"
            }
            & docker restart $workerContainer | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to restart $workerName after protocol update"
            }
            return $true
        }
        return $false
    }
    finally {
        $resolvedTemporarySoul = [System.IO.Path]::GetFullPath($temporarySoul.FullName)
        if (Test-Path -LiteralPath $resolvedTemporarySoul) {
            Remove-Item -LiteralPath $resolvedTemporarySoul -Force -ErrorAction SilentlyContinue
        }
    }
}

# Team workers are normally generated with a narrow [Leader, Admin] Matrix
# allowlist. MCUForge intentionally lets Manager dispatch and monitor work
# directly, so make that permission part of this project's bootstrap contract.
# The policy is written to both the worker config and its MinIO source of
# truth; later restarts and ordinary config updates retain Manager access.
function Ensure-ManagerWorkerAccess {
    $seedWorker = "hiclaw-worker-$($workerDefinitions[0].Name)"
    $seedPath = "/root/hiclaw-fs/agents/$($workerDefinitions[0].Name)/openclaw.json"
    $seedId = (& docker exec $seedWorker sh -lc "jq -r '.channels.matrix.groupAllowFrom[0] // empty' '$seedPath'" 2>$null | Out-String).Trim()
    if ([string]::IsNullOrWhiteSpace($seedId) -or -not $seedId.StartsWith("@") -or $seedId.IndexOf(":") -lt 2) {
        throw "Cannot derive Matrix domain from $seedWorker allowlist"
    }
    $managerId = "@manager:" + $seedId.Substring($seedId.IndexOf(":") + 1)
    # Matrix ignores inbound m.replace events.  Partial streaming turns a
    # Worker's final TASK_RECEIVED/PROGRESS text into such an edit, so the
    # Manager can see it in Element but cannot consume it.  Coordination
    # messages must therefore be emitted as one final event.
    $policyJson = '{"groupAllowExtra":["manager"],"dmAllowExtra":["manager"],"matrixStreaming":"off","blockStreaming":true}'

    foreach ($worker in $workerDefinitions) {
        $workerName = $worker.Name
        $container = "hiclaw-worker-$workerName"
        $configPath = "/root/hiclaw-fs/agents/$workerName/openclaw.json"
        $policyPath = "/root/hiclaw-fs/agents/$workerName/channel-policy.json"
        $remoteConfig = "hiclaw/hiclaw-storage/agents/$workerName/openclaw.json"
        $remotePolicy = "hiclaw/hiclaw-storage/agents/$workerName/channel-policy.json"

        $configText = (& docker exec $container sh -lc "cat '$configPath'" 2>$null | Out-String)
        if ([string]::IsNullOrWhiteSpace($configText)) {
            throw "Worker config not readable: $container$configPath"
        }
        $config = $configText | ConvertFrom-Json
        $groupAllow = @($config.channels.matrix.groupAllowFrom)
        $dmAllow = @($config.channels.matrix.dm.allowFrom)
        $policyApplied = ($groupAllow -contains $managerId) -and
            ($dmAllow -contains $managerId) -and
            ($config.channels.matrix.streaming -eq 'off') -and
            ($config.channels.matrix.blockStreaming -eq $true)

        # Always refresh the durable policy. Rewrite/restart only when the
        # running config actually lacks Manager, avoiding needless churn.
        $patchCommand = @'
set -eu
jq --arg manager '__MANAGER_ID__' '.channels.matrix.groupAllowFrom = ((.channels.matrix.groupAllowFrom // []) + [$manager] | unique) | .channels.matrix.dm.allowFrom = ((.channels.matrix.dm.allowFrom // []) + [$manager] | unique) | .channels.matrix.streaming = "off" | .channels.matrix.blockStreaming = true' '__CONFIG_PATH__' > '__TMP_PATH__'
mv '__TMP_PATH__' '__CONFIG_PATH__'
printf '%s\n' '__POLICY_JSON__' > '__POLICY_PATH__'
mc cp '__CONFIG_PATH__' '__REMOTE_CONFIG__' >/dev/null
mc cp '__POLICY_PATH__' '__REMOTE_POLICY__' >/dev/null
'@
        $patchCommand = $patchCommand.Replace('__MANAGER_ID__', $managerId).
            Replace('__CONFIG_PATH__', $configPath).
            Replace('__TMP_PATH__', "/tmp/$workerName.openclaw.json").
            Replace('__POLICY_JSON__', $policyJson).
            Replace('__POLICY_PATH__', $policyPath).
            Replace('__REMOTE_CONFIG__', $remoteConfig).
            Replace('__REMOTE_POLICY__', $remotePolicy)
        & docker exec $container sh -lc $patchCommand | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to persist Manager allowlist for $workerName"
        }

        if (-not $policyApplied) {
            & docker restart $container | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to restart $workerName after allowlist update"
            }
            $ready = $false
            $deadline = [DateTime]::UtcNow.AddMinutes(2)
            do {
                $running = (& docker inspect -f '{{.State.Running}}' $container 2>$null | Out-String).Trim()
                $runtimeText = (& docker exec $container sh -lc "cat '$configPath'" 2>$null | Out-String)
                if ($running -eq "true" -and -not [string]::IsNullOrWhiteSpace($runtimeText)) {
                    $runtimeConfig = $runtimeText | ConvertFrom-Json
                    $runtimeGroup = @($runtimeConfig.channels.matrix.groupAllowFrom)
                    $runtimeDm = @($runtimeConfig.channels.matrix.dm.allowFrom)
                    $ready = ($runtimeGroup -contains $managerId) -and
                        ($runtimeDm -contains $managerId) -and
                        ($runtimeConfig.channels.matrix.streaming -eq 'off') -and
                        ($runtimeConfig.channels.matrix.blockStreaming -eq $true)
                }
                if (-not $ready) { Start-Sleep -Seconds 3 }
            } while (-not $ready -and [DateTime]::UtcNow -lt $deadline)
            if (-not $ready) {
                throw "$workerName did not retain Manager allowlist after restart"
            }
        }
        Write-Host "  Manager access ensured: $workerName ($managerId)"
    }
}

function Ensure-ManagerWorkerRooms {
    if (-not (Test-Path -LiteralPath $HiClawEnvPath -PathType Leaf)) {
        throw "HiClaw environment file not found: $HiClawEnvPath"
    }
    $settings = @{}
    Get-Content -LiteralPath $HiClawEnvPath | Where-Object { $_ -match '^[A-Z0-9_]+=' } | ForEach-Object {
        $key, $value = $_.Split('=', 2)
        $settings[$key] = $value
    }
    foreach ($required in @('HICLAW_ADMIN_USER', 'HICLAW_ADMIN_PASSWORD', 'HICLAW_PORT_GATEWAY')) {
        if ([string]::IsNullOrWhiteSpace($settings[$required])) {
            throw "Missing $required in $HiClawEnvPath"
        }
    }

    $matrixBase = "http://127.0.0.1:$($settings['HICLAW_PORT_GATEWAY'])"
    $loginBody = @{
        type = 'm.login.password'
        identifier = @{ type = 'm.id.user'; user = $settings['HICLAW_ADMIN_USER'] }
        password = $settings['HICLAW_ADMIN_PASSWORD']
    } | ConvertTo-Json -Compress
    $login = Invoke-RestMethod -Method Post -Uri "$matrixBase/_matrix/client/v3/login" -ContentType 'application/json' -Body $loginBody
    $adminHeaders = @{ Authorization = "Bearer $($login.access_token)" }

    $managerConfigText = (& docker exec $ManagerContainer sh -lc "cat /root/manager-workspace/.openclaw/openclaw.json" | Out-String)
    if ([string]::IsNullOrWhiteSpace($managerConfigText)) {
        throw "Manager Matrix config is not readable"
    }
    $managerConfig = $managerConfigText | ConvertFrom-Json
    $managerId = $managerConfig.channels.matrix.userId
    $managerToken = $managerConfig.channels.matrix.accessToken
    if ([string]::IsNullOrWhiteSpace($managerId) -or [string]::IsNullOrWhiteSpace($managerToken)) {
        throw "Manager Matrix identity or token is missing"
    }
    $managerHeaders = @{ Authorization = "Bearer $managerToken" }

    $teamWorkers = Invoke-HiClaw -Arguments @("get", "workers", "--team", $TeamName, "-o", "json") |
        Out-String |
        ConvertFrom-Json
    foreach ($worker in @($teamWorkers.workers | Where-Object { $_.role -eq 'worker' })) {
        $roomId = $worker.roomID
        if ([string]::IsNullOrWhiteSpace($roomId)) {
            throw "Worker room is missing for $($worker.name)"
        }
        $escapedRoom = [uri]::EscapeDataString($roomId)
        $escapedManager = [uri]::EscapeDataString($managerId)
        $membership = $null
        try {
            $membership = Invoke-RestMethod -Method Get -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedRoom/state/m.room.member/$escapedManager" -Headers $adminHeaders
        }
        catch {
            $membership = $null
        }
        if ($null -eq $membership -or $membership.membership -notin @('invite', 'join')) {
            Invoke-RestMethod -Method Post -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedRoom/invite" -Headers $adminHeaders -ContentType 'application/json' -Body (@{ user_id = $managerId } | ConvertTo-Json -Compress) | Out-Null
        }
        Invoke-RestMethod -Method Post -Uri "$matrixBase/_matrix/client/v3/join/$escapedRoom" -Headers $managerHeaders -ContentType 'application/json' -Body '{}' | Out-Null
        $members = Invoke-RestMethod -Method Get -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedRoom/joined_members" -Headers $managerHeaders
        if ($members.joined.PSObject.Properties.Name -notcontains $managerId) {
            throw "Manager failed to join Worker room for $($worker.name)"
        }
        Write-Host "  Manager room membership ensured: $($worker.name) ($roomId)"
    }
}

function Ensure-WorkerManagerAccess {
    $configPath = "/root/manager-workspace/openclaw.json"
    $remoteConfig = "hiclaw/hiclaw-storage/manager/openclaw.json"
    $configText = (& docker exec $ManagerContainer sh -lc "cat '$configPath'" 2>$null | Out-String)
    if ([string]::IsNullOrWhiteSpace($configText)) {
        throw "Manager openclaw.json is not readable"
    }
    $config = $configText | ConvertFrom-Json
    $managerId = $config.channels.matrix.userId
    if ([string]::IsNullOrWhiteSpace($managerId) -or $managerId.IndexOf(':') -lt 2) {
        throw "Manager Matrix identity is missing"
    }
    $domain = $managerId.Substring($managerId.IndexOf(':') + 1)
    $workerIds = @($workerDefinitions | ForEach-Object { "@$($_.Name):$domain" })
    $requiredIds = @($workerIds + "@mcuforge-lead:$domain")
    $groupAllow = @($config.channels.matrix.groupAllowFrom)
    $dmAllow = @($config.channels.matrix.dm.allowFrom)
    $alreadyAllowed = @($requiredIds | Where-Object { $_ -notin $groupAllow -or $_ -notin $dmAllow }).Count -eq 0 -and
        $config.channels.matrix.allowBots -eq 'mentions' -and
        $config.channels.matrix.streaming -eq 'off' -and
        $config.channels.matrix.blockStreaming -eq $true
    $requiredJson = $requiredIds | ConvertTo-Json -Compress

    $patchCommand = @'
set -eu
jq --argjson workers '__WORKER_IDS__' '.channels.matrix.groupAllowFrom = ((.channels.matrix.groupAllowFrom // []) + $workers | unique) | .channels.matrix.dm.allowFrom = ((.channels.matrix.dm.allowFrom // []) + $workers | unique) | .channels.matrix.allowBots = "mentions" | .channels.matrix.streaming = "off" | .channels.matrix.blockStreaming = true' '__CONFIG_PATH__' > /tmp/mcuforge-manager-openclaw.json
mv /tmp/mcuforge-manager-openclaw.json '__CONFIG_PATH__'
mc cp '__CONFIG_PATH__' '__REMOTE_CONFIG__' >/dev/null
'@
    $patchCommand = $patchCommand.Replace('__WORKER_IDS__', $requiredJson).
        Replace('__CONFIG_PATH__', $configPath).
        Replace('__REMOTE_CONFIG__', $remoteConfig)
    & docker exec $ManagerContainer sh -lc $patchCommand | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to persist MCUForge Worker allowlist for Manager"
    }

    if (-not $alreadyAllowed) {
        & docker restart $ManagerContainer | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to restart Manager after inbound allowlist update"
        }
        $deadline = [DateTime]::UtcNow.AddMinutes(3)
        $ready = $false
        do {
            $running = (& docker inspect -f '{{.State.Running}}' $ManagerContainer 2>$null | Out-String).Trim()
            if ($running -eq 'true') {
                $health = (& docker exec $ManagerContainer openclaw health --json 2>$null | Out-String)
                if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($health)) {
                    $healthObject = $health | ConvertFrom-Json
                    $ready = $healthObject.ok -eq $true -and $healthObject.channels.matrix.probe.ok -eq $true
                }
            }
            if (-not $ready) { Start-Sleep -Seconds 3 }
        } while (-not $ready -and [DateTime]::UtcNow -lt $deadline)
        if (-not $ready) {
            throw "Manager did not become ready after inbound allowlist update"
        }
    }

    $verificationText = (& docker exec $ManagerContainer sh -lc "cat '$configPath'" | Out-String)
    $verification = $verificationText | ConvertFrom-Json
    $verifiedGroup = @($verification.channels.matrix.groupAllowFrom)
    $verifiedDm = @($verification.channels.matrix.dm.allowFrom)
    $missing = @($requiredIds | Where-Object { $_ -notin $verifiedGroup -or $_ -notin $verifiedDm })
    if ($missing.Count -gt 0 -or
        $verification.channels.matrix.allowBots -ne 'mentions' -or
        $verification.channels.matrix.streaming -ne 'off' -or
        $verification.channels.matrix.blockStreaming -ne $true) {
        throw "Manager inbound allowlist verification failed: $($missing -join ', ')"
    }
    Write-Host "  Worker access to Manager ensured: $($requiredIds.Count) identities"
}

$workersRefreshed = @($workerDefinitions | ForEach-Object { Sync-WorkerProtocol -Worker $_ }) -contains $true
if ($workersRefreshed) {
    $workerRefreshDeadline = [DateTime]::UtcNow.AddMinutes(3)
    $workersReadyAfterRefresh = $false
    do {
        $refreshedTeamState = Invoke-HiClaw -Arguments @("get", "teams", $TeamName, "-o", "json") |
            Out-String |
            ConvertFrom-Json
        if (
            $refreshedTeamState.phase -eq "Active" -and
            $refreshedTeamState.leaderReady -eq $true -and
            [int]$refreshedTeamState.readyWorkers -eq $workerDefinitions.Count
        ) {
            $workersReadyAfterRefresh = $true
            break
        }
        Start-Sleep -Seconds 5
    } while ([DateTime]::UtcNow -lt $workerRefreshDeadline)

    if (-not $workersReadyAfterRefresh) {
        throw "Workers did not become ready after protocol refresh"
    }
}

Ensure-ManagerWorkerAccess
Ensure-ManagerWorkerRooms
Ensure-WorkerManagerAccess

$projectRoomPolicyInstaller = Join-Path $scriptRoot "Install-MCUForgeProjectRoomPolicy.ps1"
if (-not (Test-Path -LiteralPath $projectRoomPolicyInstaller -PathType Leaf)) {
    throw "Project-room policy installer not found: $projectRoomPolicyInstaller"
}
& $projectRoomPolicyInstaller -ManagerContainer $ManagerContainer -Controller $Controller
if ($LASTEXITCODE -ne 0) {
    throw "Project-room policy installation failed"
}

Invoke-HiClaw -Arguments @("status")
Invoke-HiClaw -Arguments @("get", "teams", $TeamName)
Invoke-HiClaw -Arguments @("get", "workers", "--team", $TeamName)
