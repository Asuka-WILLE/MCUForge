# Export-MCUForgeImages.ps1
# 作者/维护者端：把 MCUForge 运行所需的本地 Docker 镜像导出为交付包，
# 供只有 HiClaw + Docker 的使用者离线导入（docker load）。
# 输出：artifacts/mcuforge-hiclaw-images.tar.gz + .sha256
# 使用者导入：docker load -i .\mcuforge-hiclaw-images.tar.gz

[CmdletBinding()]
param(
    [string]$WorkerImage = "local/mcuforge-hiclaw-worker:policy-safe-20260902-v2",
    [string]$WorkerControlImage = "local/mcuforge-worker-control:20260902",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\.."))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot "artifacts"
}
[void][System.IO.Directory]::CreateDirectory($OutputDirectory)

function Assert-Image {
    param([string]$Image)
    & docker image inspect $Image 1>$null 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "镜像缺失：$Image；请先运行 agent_infra\hiclaw\worker-image-policy-safe\Build-MCUForgeWorkerImage.ps1 构建。"
    }
}

Assert-Image -Image $WorkerImage
Assert-Image -Image $WorkerControlImage

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$tarPath = Join-Path $OutputDirectory "mcuforge-hiclaw-images-$stamp.tar.gz"
$shaPath = "$tarPath.sha256"

Write-Host "正在导出镜像到 $tarPath ……"
& docker save $WorkerImage $WorkerControlImage | gzip > $tarPath
if ($LASTEXITCODE -ne 0) {
    throw "docker save 失败。"
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $tarPath).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText($shaPath, "$hash  $([System.IO.Path]::GetFileName($tarPath))" + [Environment]::NewLine, [System.Text.UTF8Encoding]::new($false))

Write-Host "导出完成："
Write-Host "  $tarPath"
Write-Host "  $shaPath"
Write-Host "使用者导入命令：docker load -i $tarPath"
