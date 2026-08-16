param(
    [string]$HiClawEnvPath = "C:\Users\hz_wu\hiclaw-manager.env",
    [string]$Controller = "hiclaw-controller",
    [int]$BridgePort = 8766,
    [switch]$EnableWideAgentAccess,
    [switch]$SkipBundledSkills
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Read-EnvFile {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "HiClaw env file not found: $Path"
    }
    $result = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -notmatch '^([A-Za-z_][A-Za-z0-9_]*)=(.*)$') { continue }
        $value = $Matches[2].Trim()
        if (($value.StartsWith('"') -and $value.EndsWith('"')) -or ($value.StartsWith("'") -and $value.EndsWith("'"))) {
            $value = $value.Substring(1, $value.Length - 2)
        }
        $result[$Matches[1]] = $value
    }
    return $result
}

function Invoke-Higress {
    param(
        [ValidateSet("GET", "POST", "PUT")][string]$Method,
        [string]$Uri,
        [Microsoft.PowerShell.Commands.WebRequestSession]$Session,
        [object]$Body
    )
    try {
        $parameters = @{ Method = $Method; Uri = $Uri; WebSession = $Session; ErrorAction = "Stop" }
        if ($null -ne $Body) {
            $parameters.ContentType = "application/json"
            $parameters.Body = $Body | ConvertTo-Json -Depth 12 -Compress
        }
        return Invoke-RestMethod @parameters
    }
    catch {
        if ([int]$_.Exception.Response.StatusCode -eq 409) {
            return [pscustomobject]@{ success = $true; message = "already exists" }
        }
        throw
    }
}

$settings = Read-EnvFile -Path $HiClawEnvPath
foreach ($required in @("HICLAW_ADMIN_USER", "HICLAW_ADMIN_PASSWORD")) {
    if (-not $settings.ContainsKey($required) -or [string]::IsNullOrWhiteSpace($settings[$required])) {
        throw "Required HiClaw setting is missing: $required"
    }
}

$localAppData = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
$tokenPath = Join-Path $localAppData "MCUForge\research-web-bridge.token"
if (-not (Test-Path -LiteralPath $tokenPath -PathType Leaf)) {
    throw "Research bridge token not found. Start the Research Web Bridge once before configuring HiClaw."
}
$bridgeToken = (Get-Content -LiteralPath $tokenPath -Raw).Trim()
if ($bridgeToken.Length -lt 32) { throw "Research bridge token is invalid." }

$consolePort = if ($settings.ContainsKey("HICLAW_PORT_CONSOLE")) { [int]$settings["HICLAW_PORT_CONSOLE"] } else { 18001 }
$gatewayDomain = if ($settings.ContainsKey("HICLAW_AI_GATEWAY_DOMAIN")) { $settings["HICLAW_AI_GATEWAY_DOMAIN"] } else { "aigw-local.hiclaw.io" }
$consoleBase = "http://127.0.0.1:$consolePort"
$session = [Microsoft.PowerShell.Commands.WebRequestSession]::new()
Invoke-Higress -Method POST -Uri "$consoleBase/session/login" -Session $session -Body @{ username = $settings["HICLAW_ADMIN_USER"]; password = $settings["HICLAW_ADMIN_PASSWORD"] } | Out-Null

$consumerResponse = Invoke-Higress -Method GET -Uri "$consoleBase/v1/consumers" -Session $session -Body $null
$allowedConsumers = @("worker-mcuforge-research")
if ($EnableWideAgentAccess) {
    $allowedConsumers += @("worker-mcuforge-lead", "worker-mcuforge-requirements")
}
$availableConsumers = @($consumerResponse.data | ForEach-Object { $_.name })
$missingConsumers = @($allowedConsumers | Where-Object { $_ -notin $availableConsumers })
if ($missingConsumers.Count -gt 0) {
    throw "Required Higress consumers are missing: $($missingConsumers -join ', ')"
}

$consumerTokenHashes = [System.Collections.Generic.List[string]]::new()
foreach ($consumer in @($consumerResponse.data | Where-Object { $_.name -in $allowedConsumers })) {
    $consumerHashes = [System.Collections.Generic.List[string]]::new()
    foreach ($credential in @($consumer.credentials)) {
        foreach ($value in @($credential.values)) {
            if ([string]::IsNullOrWhiteSpace($value)) { continue }
            $bytes = [System.Text.Encoding]::UTF8.GetBytes([string]$value)
            [void]$consumerHashes.Add(([Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($bytes))).ToLowerInvariant())
        }
    }
    if ($consumerHashes.Count -eq 0) {
        throw "Research MCP consumer has no usable key-auth credential: $($consumer.name)"
    }
    foreach ($hash in $consumerHashes) {
        [void]$consumerTokenHashes.Add($hash)
    }
}
$consumerTokenHashes = @($consumerTokenHashes | Sort-Object -Unique)
if ($consumerTokenHashes.Count -eq 0) {
    throw "No authorized Research MCP consumer has a usable key-auth credential."
}
$consumerHashPath = Join-Path $localAppData "MCUForge\research-web-bridge-consumer-hashes.json"
$consumerHashJson = [ordered]@{
    version = 1
    generated_at = [DateTime]::UtcNow.ToString("o")
    consumer_names = $allowedConsumers
    token_sha256 = $consumerTokenHashes
} | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText($consumerHashPath, $consumerHashJson, [System.Text.UTF8Encoding]::new($false))

Invoke-Higress -Method POST -Uri "$consoleBase/v1/service-sources" -Session $session -Body @{
    type = "dns"
    name = "research-web-bridge-proxy"
    domain = "host.docker.internal"
    port = $BridgePort
    protocol = "http"
} | Out-Null

$rawConfiguration = @"
server:
  name: research-web-mcp-server
  type: mcp-proxy
  transport: http
  mcpServerURL: "http://host.docker.internal:$BridgePort/mcp"
  timeout: 300000
  securitySchemes:
    - id: UpstreamAuth0
      type: apiKey
      in: header
      name: X-MCUForge-Research-Token
      defaultCredential: "$bridgeToken"
  defaultUpstreamSecurity:
    id: UpstreamAuth0
"@

$mcpServerName = "mcp-research-web-bridge"
Invoke-Higress -Method PUT -Uri "$consoleBase/v1/mcpServer" -Session $session -Body @{
    name = $mcpServerName
    description = "Controlled public technical-source search and fetch bridge for MCUForge Research Worker"
    type = "OPEN_API"
    rawConfigurations = $rawConfiguration
    mcpServerName = $mcpServerName
    domains = @($gatewayDomain)
    services = @(@{ name = "research-web-bridge-proxy.dns"; port = $BridgePort; weight = 100 })
    consumerAuthInfo = @{ type = "key-auth"; enable = $true; allowedConsumers = $allowedConsumers }
} | Out-Null

Invoke-Higress -Method PUT -Uri "$consoleBase/v1/mcpServer/consumers" -Session $session -Body @{
    mcpServerName = $mcpServerName
    consumers = $allowedConsumers
} | Out-Null

$bootstrap = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\hiclaw\Bootstrap-MCUForgeTeam.ps1"))
if ($EnableWideAgentAccess) {
    if ($SkipBundledSkills) {
        & $bootstrap -Controller $Controller -EnableToolBridge -EnableResearchBridge -EnableWideAgentAccess -SkipBundledSkills
    }
    else {
        & $bootstrap -Controller $Controller -EnableToolBridge -EnableResearchBridge -EnableWideAgentAccess
    }
}
else {
    if ($SkipBundledSkills) {
        & $bootstrap -Controller $Controller -EnableToolBridge -EnableResearchBridge -SkipBundledSkills
    }
    else {
        & $bootstrap -Controller $Controller -EnableToolBridge -EnableResearchBridge
    }
}
if ($LASTEXITCODE -ne 0) { throw "Team update failed after MCP proxy registration." }

[ordered]@{
    configured = $true
    mcp_server = $mcpServerName
    upstream = "http://host.docker.internal:$BridgePort/mcp"
    authorized_consumers = $allowedConsumers
    workers_with_tool = @("mcuforge-research") + $(if ($EnableWideAgentAccess) { @("mcuforge-lead", "mcuforge-requirements") } else { @() })
    consumer_hashes_written = $consumerTokenHashes.Count
} | ConvertTo-Json -Depth 4
