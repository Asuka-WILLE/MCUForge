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
    "Write-MCUForgeJsonFile"
)
