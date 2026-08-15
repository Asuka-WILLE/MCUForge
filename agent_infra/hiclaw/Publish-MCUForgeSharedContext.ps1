param(
    [string]$RunId = "MCUFORGE-FS-001",
    [string]$CarrierWorker = "hiclaw-worker-mcuforge-firmware",
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

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptRoot)
$contextSource = Join-Path $repoRoot "agent_infra\shared_context\$RunId"

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw "docker command not found"
}
if (-not (Test-Path -LiteralPath $contextSource -PathType Container)) {
    throw "Shared context source not found: $contextSource"
}

$dirty = git -C $repoRoot status --porcelain
if ($LASTEXITCODE -ne 0) {
    throw "Unable to inspect Git status"
}
if ($dirty) {
    throw "Refusing to publish context from a dirty worktree. Commit or stash unrelated changes first."
}

$requiredFiles = @("README.md", "task-contract.yaml", "project-context.md", "source-register.yaml")
foreach ($file in $requiredFiles) {
    $path = Join-Path $contextSource $file
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required context file missing: $path"
    }
}

$head = (git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Unable to resolve Git HEAD"
}

$fileHashes = foreach ($file in $requiredFiles | Sort-Object) {
    [ordered]@{
        path = $file
        sha256 = (Get-FileHash -LiteralPath (Join-Path $contextSource $file) -Algorithm SHA256).Hash
    }
}
$manifest = [ordered]@{
    schema_version = 1
    run_id = $RunId
    git_commit = $head
    files = $fileHashes
}

$stagingParent = Join-Path ([System.IO.Path]::GetTempPath()) ("mcuforge-context-" + [Guid]::NewGuid().ToString("N"))
$stagingRoot = Join-Path $stagingParent $RunId
[System.IO.Directory]::CreateDirectory($stagingParent) | Out-Null
Copy-Item -LiteralPath $contextSource -Destination $stagingParent -Recurse
[System.IO.File]::WriteAllText(
    (Join-Path $stagingRoot "publish-manifest.json"),
    ($manifest | ConvertTo-Json -Depth 5),
    [System.Text.UTF8Encoding]::new($false)
)

$containerStaging = "/tmp/mcuforge-context-$RunId-$([Guid]::NewGuid().ToString('N'))"
$carrierRunning = (& docker inspect -f '{{.State.Running}}' $CarrierWorker).Trim()
if ($LASTEXITCODE -ne 0 -or $carrierRunning -ne "true") {
    throw "Carrier Worker is not running: $CarrierWorker"
}
& docker exec $CarrierWorker mkdir -p $containerStaging
if ($LASTEXITCODE -ne 0) {
    throw "Unable to create container staging directory"
}
& docker cp "$stagingRoot\." "${CarrierWorker}:$containerStaging"
if ($LASTEXITCODE -ne 0) {
    throw "Unable to copy shared context into Carrier Worker"
}

$publishScript = 'set -eu; run_id="$1"; local_dir="$2"; remote_dir="${HICLAW_STORAGE_PREFIX}/shared/mcuforge/runs/${run_id}"; remote_manifest="${remote_dir}/publish-manifest.json"; local_hash="$(sha256sum "${local_dir}/publish-manifest.json" | cut -d " " -f1)"; if mc stat "${remote_manifest}" >/dev/null 2>&1; then remote_hash="$(mc cat "${remote_manifest}" | sha256sum | cut -d " " -f1)"; if [ "${local_hash}" != "${remote_hash}" ]; then echo "A different context is already published for ${run_id}; create a new run_id instead of overwriting it." >&2; exit 41; fi; echo "Shared context already published and hash-identical: ${run_id}"; else mc mirror "${local_dir}/" "${remote_dir}/" --overwrite; echo "Published shared context: ${run_id}"; fi'
& docker exec $CarrierWorker sh -lc $publishScript sh $RunId $containerStaging
if ($LASTEXITCODE -ne 0) {
    throw "Shared-context publish failed"
}

foreach ($worker in $OpenClawWorkers) {
    $workerRunning = (& docker inspect -f '{{.State.Running}}' $worker).Trim()
    if ($LASTEXITCODE -ne 0 -or $workerRunning -ne "true") {
        throw "OpenClaw Worker is not running: $worker"
    }
    & docker exec $worker hiclaw-sync | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Shared-context sync failed for $worker"
    }
    & docker exec $worker test -f "/root/hiclaw-fs/shared/mcuforge/runs/$RunId/publish-manifest.json"
    if ($LASTEXITCODE -ne 0) {
        throw "Shared context is missing after sync for $worker"
    }
}

$leaderRunning = (& docker inspect -f '{{.State.Running}}' $LeaderWorker).Trim()
if ($LASTEXITCODE -ne 0 -or $leaderRunning -ne "true") {
    throw "Team Leader is not running: $LeaderWorker"
}
$leaderSyncScript = 'set -eu; run_id="$1"; remote_dir="${HICLAW_STORAGE_PREFIX}/shared/mcuforge/runs/${run_id}/"; local_dir="/root/hiclaw-fs/shared/mcuforge/runs/${run_id}/"; mkdir -p "${local_dir}"; mc mirror "${remote_dir}" "${local_dir}" --overwrite; test -f "${local_dir}/publish-manifest.json"'
& docker exec $LeaderWorker sh -lc $leaderSyncScript sh $RunId
if ($LASTEXITCODE -ne 0) {
    throw "Shared-context sync failed for Team Leader"
}

$manifestHash = (Get-FileHash -LiteralPath (Join-Path $stagingRoot "publish-manifest.json") -Algorithm SHA256).Hash
[pscustomobject]@{
    run_id = $RunId
    git_commit = $head
    publish_manifest_sha256 = $manifestHash
    source = $contextSource
    synced_workers = @($LeaderWorker) + $OpenClawWorkers
    note = "Versioned context is retained in MinIO and container staging directories are intentionally not deleted by this script."
} | ConvertTo-Json -Depth 4
