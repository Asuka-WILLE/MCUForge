param(
    [Parameter(Mandatory)][ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })][string]$ProposalDirectory,
    [Parameter(Mandatory)][string]$ApprovalToken,
    [string]$PolicyFile = (Join-Path $PSScriptRoot "patch-policy.json")
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "PatchChannel.Common.psm1") -Force
$repositoryRoot = Get-MCUForgeRepositoryRoot -StartPath $PSScriptRoot
$policy = Get-MCUForgePatchPolicy -PolicyPath $PolicyFile
Assert-MCUForgeTrackedWorktreeClean -RepositoryRoot $repositoryRoot

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

$patch = Test-MCUForgePatchFile -RepositoryRoot $repositoryRoot -PatchFile $patchPath -Policy $policy
if ($patch.Sha256 -ne $manifest.patch.sha256) {
    throw "Proposal patch hash does not match proposal.json."
}
$expectedToken = "APPLY $($manifest.proposal_id) $($manifest.patch.sha256)"
if ($ApprovalToken -cne $expectedToken) {
    throw "Approval token mismatch. Review proposal.json and enter the exact token; no changes were made."
}

$currentHead = (Invoke-MCUForgeGit -RepositoryRoot $repositoryRoot -Arguments @("rev-parse", "HEAD") | Select-Object -First 1).Trim()
if ($currentHead -ne $manifest.git.head) {
    throw "Git HEAD changed since proposal creation. Create a new proposal against the current baseline."
}
foreach ($expected in @($manifest.source_hashes)) {
    $sourcePath = Join-Path $repositoryRoot $expected.path
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Expected source file is missing: $($expected.path)"
    }
    $actualHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expected.sha256) {
        throw "Source hash changed since proposal creation: $($expected.path)"
    }
}

Invoke-MCUForgeGit -RepositoryRoot $repositoryRoot -Arguments @("apply", "--index", "--whitespace=error", "--", $patch.Path) | Out-Null
$record = [ordered]@{
    schema_version = 1
    proposal_id = $manifest.proposal_id
    status = "applied_to_git_index"
    applied_at_utc = [DateTime]::UtcNow.ToString("o")
    git_head_before_apply = $currentHead
    patch_sha256 = $patch.Sha256
    changed_paths = $patch.ChangedPaths
    next_required_actions = @(
        "Inspect git diff --cached.",
        "Run fixed-test integrity and a real Keil build.",
        "Create a normal reviewed Git commit; do not push, flash or open a COM port without separate approval."
    )
}
Write-MCUForgeJsonFile -Path (Join-Path $resolvedProposal "apply-record.json") -Value $record
$record | ConvertTo-Json -Depth 4
