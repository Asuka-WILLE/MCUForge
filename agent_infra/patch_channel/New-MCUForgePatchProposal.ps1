param(
    [Parameter(Mandatory)][ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })][string]$PatchFile,
    [Parameter(Mandatory)][ValidatePattern("^[A-Za-z0-9][A-Za-z0-9._-]{2,63}$")][string]$ProposalId,
    [string]$PolicyFile = (Join-Path $PSScriptRoot "patch-policy.json"),
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "PatchChannel.Common.psm1") -Force
$repositoryRoot = Get-MCUForgeRepositoryRoot -StartPath $PSScriptRoot
$policy = Get-MCUForgePatchPolicy -PolicyPath $PolicyFile
Assert-MCUForgeTrackedWorktreeClean -RepositoryRoot $repositoryRoot

$currentHead = (Invoke-MCUForgeGit -RepositoryRoot $repositoryRoot -Arguments @("rev-parse", "HEAD") | Select-Object -First 1).Trim()
$branch = (Invoke-MCUForgeGit -RepositoryRoot $repositoryRoot -Arguments @("branch", "--show-current") | Select-Object -First 1).Trim()
& git -C $repositoryRoot merge-base --is-ancestor $policy.Data.source_baseline_commit HEAD
if ($LASTEXITCODE -ne 0) {
    throw "Current HEAD is not descended from the policy source baseline. Create a new frozen policy for this branch."
}
$baselineDiffArguments = @(
    "diff", "--name-only", "$($policy.Data.source_baseline_commit)..HEAD", "--"
) + @($policy.Data.allowed_paths)
$sourceChangesSinceBaseline = Invoke-MCUForgeGit -RepositoryRoot $repositoryRoot -Arguments $baselineDiffArguments
if (($sourceChangesSinceBaseline -join "`n").Trim()) {
    throw "Allowed source files changed after the frozen source baseline. Freeze a new policy before proposing another patch."
}

$patch = Test-MCUForgePatchFile -RepositoryRoot $repositoryRoot -PatchFile $PatchFile -Policy $policy
$sourceHashes = Get-MCUForgeSourceHashes -RepositoryRoot $repositoryRoot -Policy $policy
$proposalRoot = Join-Path $PSScriptRoot "proposals"
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
