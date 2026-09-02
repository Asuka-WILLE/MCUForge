[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "artifacts"),
    [switch]$AllowDirtyTrackedFiles
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = $PSScriptRoot

& git -C $repoRoot rev-parse --is-inside-work-tree 1>$null
if ($LASTEXITCODE -ne 0) {
    throw "当前目录不是 Git 仓库：$repoRoot"
}

$trackedStatus = (& git -C $repoRoot status --short --untracked-files=no | Out-String).Trim()
if (-not $AllowDirtyTrackedFiles -and -not [string]::IsNullOrWhiteSpace($trackedStatus)) {
    throw "存在未提交的受跟踪文件。请先完成验证和提交，再生成交付包。"
}

$commit = (& git -C $repoRoot rev-parse HEAD | Out-String).Trim()
$shortCommit = (& git -C $repoRoot rev-parse --short=12 HEAD | Out-String).Trim()
$branch = (& git -C $repoRoot branch --show-current | Out-String).Trim()
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$packageName = "MCUForge-code-$shortCommit-$timestamp"
$zipPath = Join-Path $OutputDirectory "$packageName.zip"
$hashPath = "$zipPath.sha256"

[void][System.IO.Directory]::CreateDirectory($OutputDirectory)

& git -C $repoRoot archive --format=zip --prefix="$packageName/" --output=$zipPath HEAD
if ($LASTEXITCODE -ne 0) {
    throw "git archive 生成 ZIP 失败。"
}

$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
$hashLine = "$hash  $([System.IO.Path]::GetFileName($zipPath))"
[System.IO.File]::WriteAllText($hashPath, $hashLine + [Environment]::NewLine, [System.Text.UTF8Encoding]::new($false))

[pscustomobject]@{
    Package = $zipPath
    Sha256File = $hashPath
    Sha256 = $hash
    Branch = $branch
    Commit = $commit
} | Format-List
