param(
    [string]$ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\demos\vcw-board-demo\firmware")),
    [string]$PolicyFile = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\demos\vcw-board-demo\agent_profile\patch-policy.json"))
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "PatchChannel.Common.psm1") -Force
$projectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)
$policy = Get-MCUForgePatchPolicy -PolicyPath $PolicyFile
Assert-MCUForgeTrackedWorktreeClean -RepositoryRoot $projectRoot
$validation = Get-MCUForgeBaselineValidation -RepositoryRoot $projectRoot -Policy $policy

[ordered]@{
    status = "ready"
    policy_file = $policy.Path
    policy_sha256 = $policy.Sha256
    baseline_validation = [ordered]@{
        mode = $validation.Mode
        policy_baseline_commit = $validation.BaselineCommit
        current_head = $validation.CurrentHead
        branch = $validation.Branch
    }
    source_hashes = $validation.SourceHashes
} | ConvertTo-Json -Depth 8
