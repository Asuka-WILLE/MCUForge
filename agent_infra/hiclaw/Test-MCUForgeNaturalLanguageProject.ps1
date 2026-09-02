param(
    [Parameter(Mandatory)]
    [string]$Title,
    [string]$ManagerDmRoomId = "!MQQIeVaZ8hQvWYNZwo:matrix-local.hiclaw.io:18080",
    [string]$ManagerContainer = "hiclaw-manager",
    [string]$HiClawEnvPath = (Join-Path $env:USERPROFILE "hiclaw-manager.env"),
    [ValidateRange(30, 300)]
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$settings = @{}
Get-Content -LiteralPath $HiClawEnvPath | Where-Object { $_ -match '^[A-Z0-9_]+=' } | ForEach-Object {
    $key, $value = $_.Split('=', 2)
    $settings[$key] = $value
}
$matrixBase = "http://127.0.0.1:$($settings['HICLAW_PORT_GATEWAY'])"
$loginBody = @{
    type = 'm.login.password'
    identifier = @{ type = 'm.id.user'; user = $settings['HICLAW_ADMIN_USER'] }
    password = $settings['HICLAW_ADMIN_PASSWORD']
} | ConvertTo-Json -Compress
$login = Invoke-RestMethod -Method Post -Uri "$matrixBase/_matrix/client/v3/login" -ContentType 'application/json' -Body $loginBody
$headers = @{ Authorization = "Bearer $($login.access_token)" }
$escapedDm = [uri]::EscapeDataString($ManagerDmRoomId)

$requestMarker = "NL_PROJECT_$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
$request = "请创建一个新项目，项目名为《$Title》。本次只验证自然语言创建项目房间、全员入房和全程留痕，不要派发实际开发任务。创建完成后把后续交流迁移到新项目房间。验收标记：$requestMarker"
$txn = "mcuforge-natural-project-$([Guid]::NewGuid().ToString('N'))"
$sendBody = @{ msgtype = 'm.text'; body = $request } | ConvertTo-Json -Compress
$sent = Invoke-RestMethod -Method Put -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedDm/send/m.room.message/$txn" -Headers $headers -ContentType 'application/json' -Body $sendBody
$sentAt = [DateTime]::UtcNow

$project = $null
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
do {
    $metaLines = @(& docker exec $ManagerContainer sh -lc 'for f in /root/hiclaw-fs/shared/projects/*/meta.json; do [ -f "$f" ] && jq -c . "$f"; done' 2>$null)
    foreach ($line in $metaLines) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        try { $candidate = $line | ConvertFrom-Json } catch { continue }
        $titleProperty = $candidate.PSObject.Properties['title']
        $lifecycleProperty = $candidate.PSObject.Properties['lifecycle_state']
        $createdProperty = $candidate.PSObject.Properties['created_at']
        if ($null -ne $titleProperty -and $null -ne $lifecycleProperty -and
            $titleProperty.Value -eq $Title -and $lifecycleProperty.Value -eq 'READY') {
            $createdAt = if ($null -ne $createdProperty -and $createdProperty.Value -is [DateTime]) {
                ([DateTime]$createdProperty.Value).ToUniversalTime()
            }
            elseif ($null -ne $createdProperty -and $createdProperty.Value) {
                [DateTimeOffset]::Parse([string]$createdProperty.Value).UtcDateTime
            }
            else { [DateTime]::MinValue }
            if ($createdAt -ge $sentAt.AddSeconds(-5)) {
                $project = $candidate
                break
            }
        }
    }
    if ($null -eq $project) { Start-Sleep -Seconds 3 }
} while ($null -eq $project -and [DateTime]::UtcNow -lt $deadline)

if ($null -eq $project) {
    throw "Manager did not create a READY project room within $TimeoutSeconds seconds"
}

$dmMessages = Invoke-RestMethod -Method Get -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedDm/messages?dir=b&limit=100" -Headers $headers
$hasMoveNotice = @($dmMessages.chunk | Where-Object {
    $content = $_.PSObject.Properties['content']
    if ($null -eq $content -or $null -eq $content.Value) { return $false }
    $body = $content.Value.PSObject.Properties['body']
    $null -ne $body -and [string]$body.Value -match [regex]::Escape("project_id=$($project.project_id)")
}).Count -gt 0

$escapedProjectRoom = [uri]::EscapeDataString([string]$project.project_room_id)
$projectMessages = Invoke-RestMethod -Method Get -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedProjectRoom/messages?dir=b&limit=100" -Headers $headers
$hasOriginalRequest = @($projectMessages.chunk | Where-Object {
    $content = $_.PSObject.Properties['content']
    if ($null -eq $content -or $null -eq $content.Value) { return $false }
    $body = $content.Value.PSObject.Properties['body']
    $null -ne $body -and [string]$body.Value -match [regex]::Escape("[ORIGINAL_REQUEST]") -and
        [string]$body.Value -match [regex]::Escape($requestMarker)
}).Count -gt 0

$result = [pscustomobject]@{
    RequestEventId = $sent.event_id
    Marker = $requestMarker
    ProjectId = $project.project_id
    RoomId = $project.project_room_id
    LifecycleState = $project.lifecycle_state
    InteractionMode = $project.interaction_mode
    MoveNoticeVisible = $hasMoveNotice
    OriginalRequestVisible = $hasOriginalRequest
    Passed = $hasMoveNotice -and $hasOriginalRequest -and $project.interaction_mode -eq 'project_room_only'
}
$result | Format-List
$result | ConvertTo-Json -Compress
if (-not $result.Passed) { exit 1 }
