param(
    [string]$Controller = "hiclaw-controller",
    [string]$PackageDirectory = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\skills\general-engineering-principles-2026-08-16")),
    [string]$LeaderWorker = "hiclaw-worker-mcuforge-lead",
    [string[]]$OpenClawWorkers = @(
        "hiclaw-worker-mcuforge-requirements",
        "hiclaw-worker-mcuforge-research",
        "hiclaw-worker-mcuforge-firmware",
        "hiclaw-worker-mcuforge-verification"
    )
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw "docker command not found"
}
if (-not (Test-Path -LiteralPath $PackageDirectory -PathType Container)) {
    throw "Skill package directory not found: $PackageDirectory"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem

function Assert-RunningContainer {
    param([string]$Container)

    $running = (& docker inspect -f '{{.State.Running}}' $Container).Trim()
    if ($LASTEXITCODE -ne 0 -or $running -ne "true") {
        throw "HiClaw container is not running: $Container"
    }
}

$packages = @(Get-ChildItem -LiteralPath $PackageDirectory -File -Filter "*.skill" | Sort-Object Name)
if ($packages.Count -eq 0) {
    throw "No .skill packages found in: $PackageDirectory"
}

$stagingParent = Join-Path ([System.IO.Path]::GetTempPath()) ("mcuforge-skill-install-" + [Guid]::NewGuid().ToString("N"))
$stagingRoot = Join-Path $stagingParent "skills"
[System.IO.Directory]::CreateDirectory($stagingRoot) | Out-Null

$seenNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
$skillRecords = @()

foreach ($package in $packages) {
    $archive = [System.IO.Compression.ZipFile]::OpenRead($package.FullName)
    $reader = $null
    try {
        $skillEntries = @($archive.Entries | Where-Object { $_.FullName -match '^[^/]+/SKILL\.md$' })
        if ($skillEntries.Count -ne 1) {
            throw "Package must contain exactly one top-level <skill-name>/SKILL.md: $($package.Name)"
        }

        $entry = $skillEntries[0]
        $entryMatch = [regex]::Match($entry.FullName, '^(?<name>[a-z0-9][a-z0-9-]{1,63})/SKILL\.md$')
        if (-not $entryMatch.Success) {
            throw "Invalid skill directory name in $($package.Name): $($entry.FullName)"
        }
        $skillName = $entryMatch.Groups['name'].Value
        if (-not $seenNames.Add($skillName)) {
            throw "Duplicate skill name across packages: $skillName"
        }

        $reader = [System.IO.StreamReader]::new($entry.Open())
        $content = $reader.ReadToEnd()
        $frontMatter = [regex]::Match($content, '(?s)\A---\s*\r?\n(?<body>.*?)\r?\n---\s*\r?\n')
        if (-not $frontMatter.Success) {
            throw "SKILL.md must start with YAML front matter: $($package.Name)"
        }
        $declaredName = [regex]::Match($frontMatter.Groups['body'].Value, '(?m)^name:\s*(?<name>[a-z0-9][a-z0-9-]*)\s*$')
        $description = [regex]::Match($frontMatter.Groups['body'].Value, '(?m)^description:\s*\S.+$')
        if (-not $declaredName.Success -or $declaredName.Groups['name'].Value -ne $skillName) {
            throw "Skill name in YAML must match package directory for $($package.Name)"
        }
        if (-not $description.Success) {
            throw "Skill description is required in $($package.Name)"
        }

        $skillDirectory = Join-Path $stagingRoot $skillName
        [System.IO.Directory]::CreateDirectory($skillDirectory) | Out-Null
        $skillPath = Join-Path $skillDirectory "SKILL.md"
        [System.IO.File]::WriteAllText($skillPath, $content, [System.Text.UTF8Encoding]::new($false))
        $skillRecords += [pscustomobject]@{
            name = $skillName
            package = $package.Name
            package_sha256 = (Get-FileHash -LiteralPath $package.FullName -Algorithm SHA256).Hash
            skill_sha256 = (Get-FileHash -LiteralPath $skillPath -Algorithm SHA256).Hash
        }
    }
    finally {
        if ($null -ne $reader) {
            $reader.Dispose()
        }
        $archive.Dispose()
    }
}

Assert-RunningContainer -Container $Controller
foreach ($worker in @($LeaderWorker) + $OpenClawWorkers) {
    Assert-RunningContainer -Container $worker
}

$storagePrefix = (& docker exec $Controller printenv HICLAW_STORAGE_PREFIX).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($storagePrefix)) {
    throw "HiClaw storage prefix is unavailable from $Controller"
}

$containerStaging = "/tmp/mcuforge-skill-bundle-$([Guid]::NewGuid().ToString('N'))"
& docker exec $Controller mkdir -p $containerStaging
if ($LASTEXITCODE -ne 0) {
    throw "Unable to create controller skill staging directory"
}
& docker cp "$stagingRoot\." "${Controller}:$containerStaging"
if ($LASTEXITCODE -ne 0) {
    throw "Unable to copy skill bundle into HiClaw controller"
}

$workerTargets = @(
    [pscustomobject]@{ Container = $LeaderWorker; AgentName = "mcuforge-lead"; Runtime = "copaw" }
) + @(
    $OpenClawWorkers | ForEach-Object {
        [pscustomobject]@{ Container = $_; AgentName = $_ -replace '^hiclaw-worker-', ''; Runtime = "openclaw" }
    }
)

foreach ($target in $workerTargets) {
    $remoteSkills = "$storagePrefix/agents/$($target.AgentName)/skills/"
    & docker exec $Controller mc mirror "$containerStaging/" $remoteSkills --overwrite
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to publish skills for $($target.AgentName)"
    }
}

foreach ($target in $workerTargets | Where-Object Runtime -eq "openclaw") {
    & docker exec $target.Container hiclaw-sync | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to sync skills into $($target.AgentName)"
    }
}

$leaderSkillsRoot = "/root/hiclaw-fs/agents/mcuforge-lead/skills"
& docker exec $LeaderWorker mkdir -p $leaderSkillsRoot
if ($LASTEXITCODE -ne 0) {
    throw "Unable to create Team Leader skills directory"
}
& docker exec $LeaderWorker mc mirror "$storagePrefix/agents/mcuforge-lead/skills/" "$leaderSkillsRoot/" --overwrite
if ($LASTEXITCODE -ne 0) {
    throw "Unable to sync skills into Team Leader"
}

Start-Sleep -Milliseconds 500
foreach ($target in $workerTargets) {
    foreach ($skill in $skillRecords) {
        $workerSkill = "/root/hiclaw-fs/agents/$($target.AgentName)/skills/$($skill.name)/SKILL.md"
        & docker exec $target.Container test -f $workerSkill
        if ($LASTEXITCODE -ne 0) {
            throw "Skill $($skill.name) is missing after sync for $($target.AgentName)"
        }
    }
}

[pscustomobject]@{
    package_directory = [System.IO.Path]::GetFullPath($PackageDirectory)
    skills = $skillRecords
    installed_for = $workerTargets.AgentName
    storage_prefix = $storagePrefix
    controller_staging = $containerStaging
    local_staging = $stagingParent
    note = "Packages are validated, retained in MinIO worker workspaces, and auto-discovered from skills/<name>/SKILL.md. Staging directories are intentionally retained for audit."
} | ConvertTo-Json -Depth 5
