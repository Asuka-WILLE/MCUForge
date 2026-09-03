# Build-MCUForgeWorkerImage.ps1
# 构建 MCUForge 需要的两个本地 Docker 镜像：
#   1) local/mcuforge-hiclaw-worker:policy-safe-20260902-v2
#      在 HiClaw 官方 hiclaw-worker 基础上打策略安全层：
#      - .codex/tmp 缓存不进 Worker<->MinIO 镜像，避免巨量同步；
#      - 把 channel-policy.json（群聊/私聊白名单、streaming=off）固化为每次配置合并的
#        不变式，防止 Manager/Controller 重新生成远端 openclaw.json 时把白名单覆盖掉。
#   2) local/mcuforge-worker-control:20260902（内网 Worker 可靠性守护桥）
# 构建源（Dockerfile 与补丁脚本）都提交在仓库内，可离线重建与审计。
#
# 全新环境若没有随交付包提供 tar 镜像（docker load），直接运行本脚本即可从官方基础镜像构建。

[CmdletBinding()]
param(
    [string]$BaseImage = "higress-registry.cn-hangzhou.cr.aliyuncs.com/higress/hiclaw-worker:latest",
    [string]$WorkerImage = "local/mcuforge-hiclaw-worker:policy-safe-20260902-v2",
    [string]$WorkerControlImage = "local/mcuforge-worker-control:20260902",
    [string]$WorkerControlBase = "python:3.12-alpine",
    [switch]$SkipWorkerControl
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\.."))
$policyBuildDir = $PSScriptRoot
$controlBuildDir = Join-Path (Join-Path $PSScriptRoot "..\") "worker-control-bridge"

function Assert-DockerReady {
    & docker info 1>$null 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "Docker 引擎不可用；请先启动 Docker Desktop。"
    }
}

function Test-ImageExists {
    param([string]$Image)
    & docker image inspect $Image 1>$null 2>$null
    return $LASTEXITCODE -eq 0
}

Assert-DockerReady
Write-Host "仓库根目录：$repoRoot"

# ---------- 1) Worker Control 镜像 ----------
if (-not $SkipWorkerControl) {
    if (-not (Test-Path -LiteralPath (Join-Path $controlBuildDir "Dockerfile"))) {
        throw "找不到 worker-control-bridge 构建目录：$controlBuildDir"
    }
    if (-not (Test-ImageExists -Image $WorkerControlImage)) {
        Write-Host "[1/2] 构建 $WorkerControlImage ……"
        if (-not (Test-ImageExists -Image $WorkerControlBase)) {
            Write-Host "基础镜像 $WorkerControlBase 不在本地，尝试拉取（首次构建需要网络）。"
            & docker pull $WorkerControlBase
            if ($LASTEXITCODE -ne 0) {
                throw "拉取 $WorkerControlBase 失败；请联网后重试，或从交付包导入包含该镜像的 tar。"
            }
        }
        & docker build -q --build-arg "BASE_IMAGE=$WorkerControlBase" -t $WorkerControlImage $controlBuildDir
        if ($LASTEXITCODE -ne 0) {
            throw "构建 $WorkerControlImage 失败。"
        }
    }
    else {
        Write-Host "[1/2] $WorkerControlImage 已存在，跳过。"
    }
}

# ---------- 2) policy-safe Worker 镜像 ----------
if (-not (Test-ImageExists -Image $WorkerImage)) {
    if (-not (Test-ImageExists -Image $BaseImage)) {
        Write-Host "基础镜像 $BaseImage 不在本地，尝试拉取（约 2-4 GB，首次需要网络）。"
        & docker pull $BaseImage
        if ($LASTEXITCODE -ne 0) {
            throw "拉取 $BaseImage 失败；请联网后重试，或从交付包执行 docker load -i .\mcuforge-hiclaw-images.tar.gz 导入。"
        }
    }
    Write-Host "[2/2] 基于 $BaseImage 构建 $WorkerImage ……"
    & docker build -q --build-arg "BASE_IMAGE=$BaseImage" -f (Join-Path $policyBuildDir "Dockerfile") -t $WorkerImage $policyBuildDir
    if ($LASTEXITCODE -ne 0) {
        throw "构建 $WorkerImage 失败。"
    }
}
else {
    Write-Host "[2/2] $WorkerImage 已存在，跳过。"
}

if (-not (Test-ImageExists -Image $WorkerImage)) {
    throw "校验失败：镜像 $WorkerImage 仍未就绪。"
}
Write-Host "镜像就绪："
Write-Host "  $WorkerImage"
if (-not $SkipWorkerControl) { Write-Host "  $WorkerControlImage" }
Write-Host "可继续执行 pwsh -File .\Start-MCUForge.ps1 完成首次部署。"
