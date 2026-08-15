param(
    [string]$ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\demos\um10550-board-demo\firmware")),
    [string]$ProfileRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\..\demos\um10550-board-demo\agent_profile")),
    [string]$AgentRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..")),
    [int]$Port = 8765
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$localAppData = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
$configDirectory = Join-Path $localAppData "MCUForge"
$tokenPath = Join-Path $configDirectory "stm32-tool-bridge.token"
$consumerHashPath = Join-Path $configDirectory "stm32-tool-bridge-consumer-hashes.json"

if (-not (Test-Path -LiteralPath $configDirectory -PathType Container)) {
    [void][System.IO.Directory]::CreateDirectory($configDirectory)
}
if (-not (Test-Path -LiteralPath $tokenPath -PathType Leaf)) {
    $bytes = [byte[]]::new(32)
    [System.Security.Cryptography.RandomNumberGenerator]::Fill($bytes)
    $token = [Convert]::ToHexString($bytes)
    [System.IO.File]::WriteAllText($tokenPath, $token, [System.Text.UTF8Encoding]::new($false))
}

$env:MCUFORGE_PROJECT_ROOT = [System.IO.Path]::GetFullPath($ProjectRoot)
$env:MCUFORGE_PROFILE_ROOT = [System.IO.Path]::GetFullPath($ProfileRoot)
$env:MCUFORGE_AGENT_ROOT = [System.IO.Path]::GetFullPath($AgentRoot)
$env:MCUFORGE_BRIDGE_PORT = $Port.ToString([System.Globalization.CultureInfo]::InvariantCulture)
$env:MCUFORGE_BRIDGE_TOKEN = (Get-Content -LiteralPath $tokenPath -Raw).Trim()
$env:MCUFORGE_CONSUMER_HASH_PATH = $consumerHashPath

if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot "dist\index.js") -PathType Leaf)) {
    throw "dist/index.js is missing. Run npm install and npm run build in $PSScriptRoot first."
}
foreach ($requiredDirectory in @($env:MCUFORGE_PROJECT_ROOT, $env:MCUFORGE_PROFILE_ROOT, $env:MCUFORGE_AGENT_ROOT)) {
    if (-not (Test-Path -LiteralPath $requiredDirectory -PathType Container)) {
        throw "Required MCUForge directory does not exist: $requiredDirectory"
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $env:MCUFORGE_PROJECT_ROOT "MDK-ARM\UM10550.uvprojx") -PathType Leaf)) {
    throw "ProjectRoot is not a UM10550 firmware project: $env:MCUFORGE_PROJECT_ROOT"
}

Push-Location $PSScriptRoot
try {
    & node "dist\index.js"
    if ($LASTEXITCODE -ne 0) {
        throw "STM32 Tool Bridge exited with code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
