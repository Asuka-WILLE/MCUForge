param(
    [string]$ManagerRoot = (Join-Path $env:USERPROFILE "hiclaw-manager"),
    [string]$ManagerContainer = "hiclaw-manager",
    [string]$Controller = "hiclaw-controller",
    [switch]$SkipRestart
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$sourceRoot = Join-Path $PSScriptRoot "manager-project-room"
$sourceFiles = @(
    (Join-Path $sourceRoot "AGENTS.policy.md"),
    (Join-Path $sourceRoot "SKILL.md"),
    (Join-Path $sourceRoot "references\create-project.md"),
    (Join-Path $sourceRoot "scripts\create-project.sh")
)
foreach ($source in $sourceFiles) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Project-room policy source not found: $source"
    }
}
if (-not (Test-Path -LiteralPath $ManagerRoot -PathType Container)) {
    throw "HiClaw Manager workspace not found: $ManagerRoot"
}

function Get-ContentHash {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return "missing" }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Copy-PolicyFile {
    param([string]$Source, [string]$Destination)
    $before = Get-ContentHash -Path $Destination
    $parent = Split-Path -Parent $Destination
    [void](New-Item -ItemType Directory -Path $parent -Force)
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
    return $before -ne (Get-ContentHash -Path $Destination)
}

$changed = $false
$customSkillRoot = Join-Path $ManagerRoot "skills\project-room-lifecycle"
$builtinSkillRoot = Join-Path $ManagerRoot "skills\project-management"

$copyMap = @(
    @{ Source = (Join-Path $sourceRoot "SKILL.md"); Destination = (Join-Path $customSkillRoot "SKILL.md") },
    @{ Source = (Join-Path $sourceRoot "references\create-project.md"); Destination = (Join-Path $customSkillRoot "references\create-project.md") },
    @{ Source = (Join-Path $sourceRoot "scripts\create-project.sh"); Destination = (Join-Path $customSkillRoot "scripts\create-project.sh") },
    @{ Source = (Join-Path $sourceRoot "references\create-project.md"); Destination = (Join-Path $builtinSkillRoot "references\create-project.md") },
    @{ Source = (Join-Path $sourceRoot "scripts\create-project.sh"); Destination = (Join-Path $builtinSkillRoot "scripts\create-project.sh") }
)
foreach ($entry in $copyMap) {
    if (Copy-PolicyFile -Source $entry.Source -Destination $entry.Destination) {
        $changed = $true
    }
}

$agentsPath = Join-Path $ManagerRoot "AGENTS.md"
if (-not (Test-Path -LiteralPath $agentsPath -PathType Leaf)) {
    throw "Manager AGENTS.md not found: $agentsPath"
}
$policy = (Get-Content -LiteralPath (Join-Path $sourceRoot "AGENTS.policy.md") -Raw).Trim()
$startMarker = "<!-- mcuforge-project-room-policy-start -->"
$endMarker = "<!-- mcuforge-project-room-policy-end -->"
$managedBlock = "$startMarker`n$policy`n$endMarker"
$agentsText = Get-Content -LiteralPath $agentsPath -Raw
$pattern = "(?s)" + [regex]::Escape($startMarker) + ".*?" + [regex]::Escape($endMarker)
$updatedAgents = if ($agentsText -match $pattern) {
    [regex]::Replace($agentsText, $pattern, [System.Text.RegularExpressions.MatchEvaluator]{ param($match) $managedBlock })
}
else {
    $agentsText.TrimEnd() + "`n`n" + $managedBlock + "`n"
}
if ($updatedAgents -cne $agentsText) {
    [System.IO.File]::WriteAllText($agentsPath, $updatedAgents, [System.Text.UTF8Encoding]::new($false))
    $changed = $true
}

& docker exec $ManagerContainer sh -lc 'chmod +x /root/manager-workspace/skills/project-room-lifecycle/scripts/create-project.sh /root/manager-workspace/skills/project-management/scripts/create-project.sh'
if ($LASTEXITCODE -ne 0) {
    throw "Unable to mark project-room scripts executable"
}

# The built-in project-management prompt refers to /opt/hiclaw/agent.  Keep
# that live path aligned as well; otherwise the model can bypass the durable
# workspace copy and silently fall back to the v1 DM-based flow.
$runtimeGuide = Join-Path $sourceRoot "references\create-project.md"
$runtimeScript = Join-Path $sourceRoot "scripts\create-project.sh"
& docker cp $runtimeGuide "${ManagerContainer}:/opt/hiclaw/agent/skills/project-management/references/create-project.md"
if ($LASTEXITCODE -ne 0) { throw "Unable to update runtime project-management guide" }
& docker cp $runtimeScript "${ManagerContainer}:/opt/hiclaw/agent/skills/project-management/scripts/create-project.sh"
if ($LASTEXITCODE -ne 0) { throw "Unable to update runtime project-management script" }
& docker exec $ManagerContainer chmod +x /opt/hiclaw/agent/skills/project-management/scripts/create-project.sh
if ($LASTEXITCODE -ne 0) { throw "Unable to mark runtime project-management script executable" }

$storagePrefix = (& docker exec $Controller printenv HICLAW_STORAGE_PREFIX | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($storagePrefix)) {
    throw "HiClaw storage prefix is unavailable"
}
$publishCommand = @'
set -eu
mc mirror /root/manager-workspace/skills/project-room-lifecycle/ '__STORAGE__/manager/skills/project-room-lifecycle/' --overwrite >/dev/null
mc mirror /root/manager-workspace/skills/project-management/ '__STORAGE__/manager/skills/project-management/' --overwrite >/dev/null
mc cp /root/manager-workspace/AGENTS.md '__STORAGE__/manager/AGENTS.md' >/dev/null
'@.Replace('__STORAGE__', $storagePrefix)
& docker exec $ManagerContainer sh -lc $publishCommand
if ($LASTEXITCODE -ne 0) {
    throw "Unable to publish Manager project-room policy to MinIO"
}

if ($changed -and -not $SkipRestart) {
    & docker restart $ManagerContainer | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to restart Manager after project-room policy update"
    }
    $deadline = [DateTime]::UtcNow.AddMinutes(3)
    $ready = $false
    do {
        $healthText = (& docker exec $ManagerContainer openclaw health --json 2>$null | Out-String)
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($healthText)) {
            $health = $healthText | ConvertFrom-Json
            $ready = $health.ok -eq $true -and $health.channels.matrix.probe.ok -eq $true
        }
        if (-not $ready) { Start-Sleep -Seconds 3 }
    } while (-not $ready -and [DateTime]::UtcNow -lt $deadline)
    if (-not $ready) {
        throw "Manager did not become ready after project-room policy update"
    }

    # Re-assert container-layer overrides after restart. Docker restart keeps
    # them today; this also documents and verifies the intended live paths.
    & docker cp $runtimeGuide "${ManagerContainer}:/opt/hiclaw/agent/skills/project-management/references/create-project.md"
    if ($LASTEXITCODE -ne 0) { throw "Unable to restore runtime project guide after restart" }
    & docker cp $runtimeScript "${ManagerContainer}:/opt/hiclaw/agent/skills/project-management/scripts/create-project.sh"
    if ($LASTEXITCODE -ne 0) { throw "Unable to restore runtime project script after restart" }
    & docker exec $ManagerContainer chmod +x /opt/hiclaw/agent/skills/project-management/scripts/create-project.sh
    if ($LASTEXITCODE -ne 0) { throw "Unable to restore runtime project script permissions" }
}

[pscustomobject]@{
    changed = $changed
    manager_root = [System.IO.Path]::GetFullPath($ManagerRoot)
    custom_skill = "project-room-lifecycle"
    builtin_project_skill_overridden = $true
    restarted = $changed -and -not $SkipRestart
    storage_prefix = $storagePrefix
} | ConvertTo-Json -Compress
