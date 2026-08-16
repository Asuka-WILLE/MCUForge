param(
    [Parameter(Mandatory)][ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })][string]$ProposalDirectory,
    [Parameter(Mandatory)][string]$ApprovalToken,
    [string]$ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\demos\um10550-board-demo\firmware")),
    [string]$ProfileRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\demos\um10550-board-demo\agent_profile")),
    [string]$PolicyFile = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\demos\um10550-board-demo\agent_profile\patch-policy.json"))
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "PatchChannel.Common.psm1") -Force
$projectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
$profileRoot = [System.IO.Path]::GetFullPath($ProfileRoot)
$repositoryRoot = Get-MCUForgeRepositoryRoot -StartPath $projectRoot
$policy = Get-MCUForgePatchPolicy -PolicyPath $PolicyFile
Assert-MCUForgeTrackedWorktreeClean -RepositoryRoot $projectRoot

$resolvedProposal = (Resolve-Path -LiteralPath $ProposalDirectory).Path
$manifestPath = Join-Path $resolvedProposal "proposal.json"
$patchPath = Join-Path $resolvedProposal "proposal.patch"
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf) -or -not (Test-Path -LiteralPath $patchPath -PathType Leaf)) {
    throw "Proposal must contain proposal.json and proposal.patch."
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.status -ne "pending_human_review") {
    throw "Only pending_human_review proposals may be applied. Current status: $($manifest.status)"
}
if ($manifest.contract.policy_sha256 -ne $policy.Sha256) {
    throw "Patch policy changed after proposal creation. Create and review a new proposal."
}

$patch = Test-MCUForgePatchFile -RepositoryRoot $projectRoot -PatchFile $patchPath -Policy $policy
if ($patch.Sha256 -ne $manifest.patch.sha256) {
    throw "Proposal patch hash does not match proposal.json."
}
$expectedToken = "APPLY $($manifest.proposal_id) $($manifest.patch.sha256)"
if ($ApprovalToken -cne $expectedToken) {
    throw "Approval token mismatch. Review proposal.json and enter the exact token; no changes were made."
}

$currentHead = (Invoke-MCUForgeGit -RepositoryRoot $projectRoot -Arguments @("rev-parse", "HEAD") | Select-Object -First 1).Trim()
if ($currentHead -ne $manifest.git.head) {
    throw "Git HEAD changed since proposal creation. Create a new proposal against the current baseline."
}
foreach ($expected in @($manifest.source_hashes)) {
    $sourcePath = Join-Path $projectRoot $expected.path
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Expected source file is missing: $($expected.path)"
    }
    $actualHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expected.sha256) {
        throw "Source hash changed since proposal creation: $($expected.path)"
    }
}

$projectRelative = [System.IO.Path]::GetRelativePath($repositoryRoot, $projectRoot).Replace("\", "/")
$directoryArguments = @()
if ($projectRelative -ne ".") {
    $directoryArguments = @("--directory=$projectRelative")
}
$applyArguments = @("apply", "--index", "--verbose", "--whitespace=error") + $directoryArguments + @("--", $patch.Path)
$applyOutput = @(Invoke-MCUForgeGit -RepositoryRoot $repositoryRoot -Arguments $applyArguments 2>&1)
$skippedPatches = @($applyOutput | Where-Object { $_.ToString() -match "(?i)\bSkipped patch\b" })
if ($skippedPatches.Count -gt 0) {
    throw "Git reported a skipped patch even though the apply command returned success: $($skippedPatches -join ' ' )"
}

$expectedPaths = @($patch.ChangedPaths)
$stagedPaths = @(Invoke-MCUForgeGit -RepositoryRoot $projectRoot -Arguments @("diff", "--cached", "--name-only", "--") | ForEach-Object { ([string]$_).Trim() } | Where-Object { $_ })
$worktreePaths = @(Invoke-MCUForgeGit -RepositoryRoot $projectRoot -Arguments @("diff", "--name-only", "--") | ForEach-Object { ([string]$_).Trim() } | Where-Object { $_ })
$missingStaged = @($expectedPaths | Where-Object { $stagedPaths -notcontains $_ })
$unexpectedStaged = @($stagedPaths | Where-Object { $expectedPaths -notcontains $_ })
if ($missingStaged.Count -gt 0 -or $unexpectedStaged.Count -gt 0) {
    throw "Indexed patch postcondition failed. Expected staged paths=$($expectedPaths -join ','); actual staged paths=$($stagedPaths -join ','); missing=$($missingStaged -join ','); unexpected=$($unexpectedStaged -join ',')."
}
if ($worktreePaths.Count -gt 0) {
    throw "Indexed patch postcondition failed: worktree has unstaged paths after apply: $($worktreePaths -join ', ')"
}

$sourceHashesAfter = foreach ($expected in @($manifest.source_hashes)) {
    $sourcePath = Join-Path $projectRoot $expected.path
    [ordered]@{
        path = [string]$expected.path
        sha256 = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
foreach ($expected in @($manifest.source_hashes | Where-Object { $patch.ChangedPaths -contains $_.path })) {
    $actual = @($sourceHashesAfter | Where-Object { $_.path -eq $expected.path })[0]
    if ($null -eq $actual -or $actual.sha256 -eq ([string]$expected.sha256).ToLowerInvariant()) {
        throw "Indexed patch postcondition failed: source hash did not change for $($expected.path)."
    }
}

$record = [ordered]@{
    schema_version = 1
    proposal_id = $manifest.proposal_id
    status = "applied_to_git_index"
    applied_at_utc = [DateTime]::UtcNow.ToString("o")
    git_head_before_apply = $currentHead
    git_root = $repositoryRoot
    project_relative = $projectRelative
    patch_sha256 = $patch.Sha256
    changed_paths = $patch.ChangedPaths
    materialized_to_worktree = $true
    staged_paths = $stagedPaths
    unstaged_paths = $worktreePaths
    source_hashes_after = $sourceHashesAfter
    next_required_actions = @(
        "Inspect git diff --cached.",
        "Run fixed-test integrity and a real Keil build.",
        "Create a normal reviewed Git commit; do not push, flash or open a COM port without separate approval."
    )
}
Write-MCUForgeJsonFile -Path (Join-Path $resolvedProposal "apply-record.json") -Value $record
$record | ConvertTo-Json -Depth 4
