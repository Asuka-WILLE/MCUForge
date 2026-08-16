param(
    [Parameter(Mandatory)][ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })][string]$PatchFile,
    [Parameter(Mandatory)][ValidatePattern("^[A-Za-z0-9][A-Za-z0-9._-]{2,63}$")][string]$ProposalId,
    [string]$ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\demos\vcw-board-demo\firmware")),
    [string]$ProfileRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\demos\vcw-board-demo\agent_profile")),
    [string]$PolicyFile = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\demos\vcw-board-demo\agent_profile\patch-policy.json")),
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "PatchChannel.Common.psm1") -Force
$projectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
$profileRoot = [System.IO.Path]::GetFullPath($ProfileRoot)
$repositoryRoot = Get-MCUForgeRepositoryRoot -StartPath $projectRoot
$policy = Get-MCUForgePatchPolicy -PolicyPath $PolicyFile
Assert-MCUForgeTrackedWorktreeClean -RepositoryRoot $projectRoot

$baseline = Get-MCUForgeBaselineValidation -RepositoryRoot $projectRoot -Policy $policy
$currentHead = $baseline.CurrentHead
$branch = $baseline.Branch

$patch = Test-MCUForgePatchFile -RepositoryRoot $projectRoot -PatchFile $PatchFile -Policy $policy
$sourceHashes = $baseline.SourceHashes
$proposalRoot = Join-Path $profileRoot "patch_proposals"
$proposalDirectory = Join-Path $proposalRoot $ProposalId
if (Test-Path -LiteralPath $proposalDirectory) {
    throw "Proposal directory already exists and will not be overwritten: $proposalDirectory"
}

$manifest = [ordered]@{
    schema_version = 1
    proposal_id = $ProposalId
    status = "pending_human_review"
    created_at_utc = [DateTime]::UtcNow.ToString("o")
    contract = [ordered]@{
        run_id = $policy.Data.run_id
        contract_version = $policy.Data.contract_version
        source_baseline_commit = $policy.Data.source_baseline_commit
        policy_sha256 = $policy.Sha256
    }
    git = [ordered]@{
        branch = $branch
        head = $currentHead
        project_root = [System.IO.Path]::GetRelativePath($repositoryRoot, $projectRoot).Replace("\", "/")
    }
    baseline_validation = [ordered]@{
        mode = $baseline.Mode
        policy_baseline_commit = $baseline.BaselineCommit
        source_hashes_match = $true
    }
    patch = [ordered]@{
        file = "proposal.patch"
        sha256 = $patch.Sha256
        bytes = $patch.Bytes
        changed_paths = $patch.ChangedPaths
    }
    source_hashes = $sourceHashes
    required_human_actions = @(
        "Review proposal.patch and proposal.json.",
        "Run the independent test-integrity check and Keil build after apply.",
        "Enter the exact approval token only when ready to stage this patch.",
        "Make a normal Git commit only after review and verification."
    )
    prohibited_actions_not_taken = $policy.Data.prohibited_actions
}

if ($DryRun) {
    [pscustomobject]@{
        proposal_id = $ProposalId
        status = "validated_not_recorded"
        changed_paths = $patch.ChangedPaths
        patch_sha256 = $patch.Sha256
        next_action = "Review the patch and run again without -DryRun to create an auditable pending proposal."
    } | ConvertTo-Json -Depth 4
    return
}

[void](New-Item -ItemType Directory -Path $proposalDirectory -ErrorAction Stop)
Copy-Item -LiteralPath $patch.Path -Destination (Join-Path $proposalDirectory "proposal.patch") -ErrorAction Stop
Write-MCUForgeJsonFile -Path (Join-Path $proposalDirectory "proposal.json") -Value $manifest

[pscustomobject]@{
    proposal_id = $ProposalId
    status = "pending_human_review"
    directory = $proposalDirectory
    changed_paths = $patch.ChangedPaths
    patch_sha256 = $patch.Sha256
    next_action = "Review proposal.patch. To stage it after explicit approval, run Apply-MCUForgeApprovedPatch.ps1 with the exact token shown in proposal.json."
} | ConvertTo-Json -Depth 4
