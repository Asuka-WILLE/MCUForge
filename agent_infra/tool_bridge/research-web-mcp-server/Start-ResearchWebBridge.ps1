param(
    [int]$Port = 8766
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$localAppData = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
$configDirectory = Join-Path $localAppData "MCUForge"
$tokenPath = Join-Path $configDirectory "research-web-bridge.token"
$consumerHashPath = Join-Path $configDirectory "research-web-bridge-consumer-hashes.json"

if (-not (Test-Path -LiteralPath $configDirectory -PathType Container)) {
    [void][System.IO.Directory]::CreateDirectory($configDirectory)
}
if (-not (Test-Path -LiteralPath $tokenPath -PathType Leaf)) {
    $bytes = [byte[]]::new(32)
    [System.Security.Cryptography.RandomNumberGenerator]::Fill($bytes)
    [System.IO.File]::WriteAllText($tokenPath, [Convert]::ToHexString($bytes), [System.Text.UTF8Encoding]::new($false))
}

$env:MCUFORGE_RESEARCH_PORT = $Port.ToString([System.Globalization.CultureInfo]::InvariantCulture)
$env:MCUFORGE_RESEARCH_TOKEN = (Get-Content -LiteralPath $tokenPath -Raw).Trim()
$env:MCUFORGE_RESEARCH_CONSUMER_HASH_PATH = $consumerHashPath

if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot "dist\index.js") -PathType Leaf)) {
    throw "dist/index.js is missing. Run npm install and npm run build in $PSScriptRoot first."
}

Push-Location $PSScriptRoot
try {
    & node "dist\index.js"
    if ($LASTEXITCODE -ne 0) {
        throw "Research Web Bridge exited with code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
