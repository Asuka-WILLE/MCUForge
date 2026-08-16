Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-MCUForgeRepositoryRoot {
    param([Parameter(Mandatory)][string]$StartPath)

    $item = Get-Item -LiteralPath $StartPath -ErrorAction Stop
    $candidate = if ($item.PSIsContainer) { $item } else { $item.Directory }
    while ($null -ne $candidate) {
        if (Test-Path -LiteralPath (Join-Path $candidate.FullName ".git")) {
            return $candidate.FullName
        }
        $candidate = $candidate.Parent
    }
    throw "Could not locate the Git repository root from '$StartPath'."
}

function Invoke-MCUForgeGit {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string[]]$Arguments
    )

    $result = & git -C $RepositoryRoot @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Git command failed: git -C '$RepositoryRoot' $($Arguments -join ' ')"
    }
    return $result
}

function Get-MCUForgePatchPolicy {
    param([Parameter(Mandatory)][string]$PolicyPath)

    $resolved = (Resolve-Path -LiteralPath $PolicyPath -ErrorAction Stop).Path
    $policy = Get-Content -LiteralPath $resolved -Raw | ConvertFrom-Json
    foreach ($property in @("run_id", "contract_version", "source_baseline_commit", "patch_rule", "max_patch_bytes", "allowed_paths", "immutable_paths")) {
        if ($null -eq $policy.$property) {
            throw "Patch policy is missing '$property': $resolved"
        }
    }
    if ($policy.patch_rule -ne "modify_existing_files_only") {
        throw "Unsupported patch policy rule: $($policy.patch_rule)"
    }
    if ([int]$policy.max_patch_bytes -lt 1 -or @($policy.allowed_paths).Count -eq 0) {
        throw "Patch policy has invalid size limit or allowed paths."
    }
    if ($null -ne $policy.allow_non_source_history_drift -and
        [bool]$policy.allow_non_source_history_drift -and
        @($policy.baseline_source_hashes).Count -eq 0) {
        throw "Patch policy enables non-source history drift but has no baseline_source_hashes."
    }
    return [pscustomobject]@{
        Path = $resolved
        Sha256 = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant()
        Data = $policy
    }
}

function Assert-MCUForgeTrackedWorktreeClean {
    param([Parameter(Mandatory)][string]$RepositoryRoot)

    $status = Invoke-MCUForgeGit -RepositoryRoot $RepositoryRoot -Arguments @("status", "--porcelain", "--untracked-files=no")
    if (($status -join "`n").Trim()) {
        throw "Tracked worktree is not clean. Commit or stash tracked changes before proposing or applying a patch."
    }
}

function Normalize-MCUForgeRepositoryPath {
    param([Parameter(Mandatory)][string]$Path)

    $normalized = $Path.Replace("\", "/")
    if ($normalized.StartsWith("a/") -or $normalized.StartsWith("b/")) {
        $normalized = $normalized.Substring(2)
    }
    if ([System.IO.Path]::IsPathRooted($normalized) -or $normalized.StartsWith("../") -or $normalized.Contains("/../") -or $normalized.Contains("://")) {
        throw "Patch contains an unsafe repository path: $Path"
    }
    return $normalized
}

function Test-MCUForgePatchFile {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$PatchFile,
        [Parameter(Mandatory)]$Policy
    )

    $resolvedPatch = (Resolve-Path -LiteralPath $PatchFile -ErrorAction Stop).Path
    $patchBytes = [System.IO.File]::ReadAllBytes($resolvedPatch)
    if ($patchBytes.Length -eq 0 -or $patchBytes.Length -gt [int]$Policy.Data.max_patch_bytes) {
        throw "Patch size must be between 1 and $($Policy.Data.max_patch_bytes) bytes."
    }
    if ($patchBytes -contains 0) {
        throw "Binary patches are not allowed."
    }
    $patchText = [System.Text.Encoding]::UTF8.GetString($patchBytes)
    if ($patchText -match "(?m)^(new file mode|deleted file mode|similarity index|rename from|rename to|copy from|copy to|Binary files|GIT binary patch)") {
        throw "Patch contains a file-creation, deletion, rename, copy or binary operation."
    }

    $allowed = @($Policy.Data.allowed_paths | ForEach-Object { [string]$_ })
    $immutable = @($Policy.Data.immutable_paths | ForEach-Object { [string]$_ })
    $changed = [System.Collections.Generic.List[string]]::new()
    $headers = [regex]::Matches($patchText, "(?m)^diff --git a/([^\s]+) b/([^\s]+)\r?$")
    if ($headers.Count -eq 0) {
        throw "Patch does not contain a standard 'diff --git' header."
    }
    foreach ($header in $headers) {
        $oldPath = Normalize-MCUForgeRepositoryPath -Path $header.Groups[1].Value
        $newPath = Normalize-MCUForgeRepositoryPath -Path $header.Groups[2].Value
        if ($oldPath -ne $newPath) {
            throw "Patch may only modify an existing file; path transition '$oldPath' -> '$newPath' is forbidden."
        }
        if ($immutable -contains $oldPath) {
            throw "Patch touches an immutable path: $oldPath"
        }
        if ($allowed -notcontains $oldPath) {
            throw "Patch path is outside the frozen allowlist: $oldPath"
        }
        if (-not $changed.Contains($oldPath)) {
            [void]$changed.Add($oldPath)
        }
    }

    foreach ($line in ($patchText -split "`r?`n")) {
        if ($line -notmatch "^(---|\+\+\+) (.+)$") { continue }
        $headerPath = $Matches[2].Split("`t")[0]
        if ($headerPath -eq "/dev/null") {
            throw "Patch may not create or delete files."
        }
        $normalized = Normalize-MCUForgeRepositoryPath -Path $headerPath
        if ($immutable -contains $normalized -or $allowed -notcontains $normalized) {
            throw "Patch file header is outside the frozen allowlist: $normalized"
        }
    }

    Invoke-MCUForgeGit -RepositoryRoot $RepositoryRoot -Arguments @("apply", "--check", "--whitespace=error", "--", $resolvedPatch) | Out-Null
    return [pscustomobject]@{
        Path = $resolvedPatch
        Sha256 = (Get-FileHash -LiteralPath $resolvedPatch -Algorithm SHA256).Hash.ToLowerInvariant()
        Bytes = $patchBytes.Length
        ChangedPaths = @($changed)
    }
}

function Get-MCUForgeSourceHashes {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)]$Policy
    )

    $hashes = foreach ($relativePath in @($Policy.Data.allowed_paths)) {
        $fullPath = Join-Path $RepositoryRoot $relativePath
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            throw "Allowed source file does not exist: $relativePath"
        }
        [ordered]@{
            path = [string]$relativePath
            sha256 = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    return @($hashes)
}

function Compare-MCUForgeSourceHashes {
    param(
        [Parameter(Mandatory)]$Expected,
        [Parameter(Mandatory)]$Actual
    )

    $expectedMap = @{}
    foreach ($item in @($Expected)) {
        if ($null -eq $item.path -or $null -eq $item.sha256) {
            return $false
        }
        $expectedMap[[string]$item.path] = ([string]$item.sha256).ToLowerInvariant()
    }
    foreach ($item in @($Actual)) {
        $path = [string]$item.path
        $hash = ([string]$item.sha256).ToLowerInvariant()
        if (-not $expectedMap.ContainsKey($path) -or $expectedMap[$path] -ne $hash) {
            return $false
        }
    }
    return $expectedMap.Count -eq @($Actual).Count
}

function Get-MCUForgeBaselineValidation {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)]$Policy
    )

    $currentHead = (Invoke-MCUForgeGit -RepositoryRoot $RepositoryRoot -Arguments @("rev-parse", "HEAD") | Select-Object -First 1).Trim()
    $branch = (Invoke-MCUForgeGit -RepositoryRoot $RepositoryRoot -Arguments @("branch", "--show-current") | Select-Object -First 1).Trim()
    if ($null -ne $Policy.Data.source_baseline_branch -and
        ([string]$Policy.Data.source_baseline_branch).Trim() -and
        $branch -ne ([string]$Policy.Data.source_baseline_branch).Trim()) {
        throw "Current branch does not match the policy source baseline branch. Policy=$($Policy.Data.source_baseline_branch) Current=$branch"
    }
    $baselineCommit = ([string]$Policy.Data.source_baseline_commit).Trim()
    & git -C $RepositoryRoot cat-file -e "$baselineCommit`^{commit}" 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "Policy source baseline commit does not exist in this repository: $baselineCommit"
    }

    $sourceHashes = Get-MCUForgeSourceHashes -RepositoryRoot $RepositoryRoot -Policy $Policy
    $isAncestor = $false
    & git -C $RepositoryRoot merge-base --is-ancestor $baselineCommit HEAD 2>$null
    if ($LASTEXITCODE -eq 0) {
        $isAncestor = $true
    }

    if ($isAncestor) {
        $baselineDiffArguments = @(
            "diff", "--name-only", "$baselineCommit..HEAD", "--"
        ) + @($Policy.Data.allowed_paths)
        $sourceChangesSinceBaseline = Invoke-MCUForgeGit -RepositoryRoot $RepositoryRoot -Arguments $baselineDiffArguments
        if (($sourceChangesSinceBaseline -join "`n").Trim()) {
            throw "Allowed source files changed after the frozen source baseline. Freeze a new policy before proposing another patch. Changed: $($sourceChangesSinceBaseline -join ', ')"
        }
        return [pscustomobject]@{
            Mode = if ($currentHead -eq $baselineCommit) { "exact_baseline" } else { "descendant_non_source_drift" }
            Branch = $branch
            CurrentHead = $currentHead
            BaselineCommit = $baselineCommit
            SourceHashes = $sourceHashes
        }
    }

    $allowNonSourceHistoryDrift = $false
    if ($null -ne $Policy.Data.allow_non_source_history_drift) {
        $allowNonSourceHistoryDrift = [bool]$Policy.Data.allow_non_source_history_drift
    }
    $baselineSourceHashes = @($Policy.Data.baseline_source_hashes)
    if (-not $allowNonSourceHistoryDrift -or $baselineSourceHashes.Count -eq 0 -or
        -not (Compare-MCUForgeSourceHashes -Expected $baselineSourceHashes -Actual $sourceHashes)) {
        throw "Git history diverged from the policy source baseline and the allowed source hashes do not prove a non-source-only change. Freeze a new policy for this branch. Baseline=$baselineCommit Current=$currentHead"
    }

    return [pscustomobject]@{
        Mode = "diverged_non_source_same_source_hash"
        Branch = $branch
        CurrentHead = $currentHead
        BaselineCommit = $baselineCommit
        SourceHashes = $sourceHashes
    }
}

function Write-MCUForgeJsonFile {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)]$Value
    )

    $json = $Value | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText($Path, $json + [Environment]::NewLine, [System.Text.UTF8Encoding]::new($false))
}

Export-ModuleMember -Function @(
    "Get-MCUForgeRepositoryRoot",
    "Invoke-MCUForgeGit",
    "Get-MCUForgePatchPolicy",
    "Assert-MCUForgeTrackedWorktreeClean",
    "Test-MCUForgePatchFile",
    "Get-MCUForgeSourceHashes",
    "Compare-MCUForgeSourceHashes",
    "Get-MCUForgeBaselineValidation",
    "Write-MCUForgeJsonFile"
)
