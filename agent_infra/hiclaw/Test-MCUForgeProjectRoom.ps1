param(
    [Parameter(Mandatory)]
    [string]$ProjectId,
    [string]$ManagerContainer = "hiclaw-manager",
    [string]$Controller = "hiclaw-controller",
    [string]$HiClawEnvPath = (Join-Path $env:USERPROFILE "hiclaw-manager.env")
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$metaPath = "/root/hiclaw-fs/shared/projects/$ProjectId/meta.json"
$metaText = (& docker exec $ManagerContainer sh -lc "cat '$metaPath'" | Out-String)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($metaText)) {
    throw "Project metadata not found: $ProjectId"
}
$meta = $metaText | ConvertFrom-Json
$roomId = [string]$meta.project_room_id
if ([string]::IsNullOrWhiteSpace($roomId)) {
    throw "Project room id is missing: $ProjectId"
}

$settings = @{}
Get-Content -LiteralPath $HiClawEnvPath | Where-Object { $_ -match '^[A-Z0-9_]+=' } | ForEach-Object {
    $key, $value = $_.Split('=', 2)
    $settings[$key] = $value
}
$matrixBase = "http://127.0.0.1:$($settings['HICLAW_PORT_GATEWAY'])"
$managerConfigText = (& docker exec $ManagerContainer sh -lc 'cat /root/manager-workspace/openclaw.json' | Out-String)
$managerConfig = $managerConfigText | ConvertFrom-Json
$headers = @{ Authorization = "Bearer $($managerConfig.channels.matrix.accessToken)" }
$escapedRoom = [uri]::EscapeDataString($roomId)
$members = Invoke-RestMethod -Method Get -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedRoom/joined_members" -Headers $headers
$joined = @($members.joined.PSObject.Properties.Name)
$expected = @($meta.participants)
$missing = @($expected | Where-Object { $_ -notin $joined })

$roomName = Invoke-RestMethod -Method Get -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedRoom/state/m.room.name" -Headers $headers
$roomTopic = Invoke-RestMethod -Method Get -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedRoom/state/m.room.topic" -Headers $headers
$messages = Invoke-RestMethod -Method Get -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedRoom/messages?dir=b&limit=100" -Headers $headers
$hasCreatedTrace = @($messages.chunk | Where-Object {
    $contentProperty = $_.PSObject.Properties['content']
    if ($null -eq $contentProperty -or $null -eq $contentProperty.Value) { return $false }
    $bodyProperty = $contentProperty.Value.PSObject.Properties['body']
    return $null -ne $bodyProperty -and
        [string]$bodyProperty.Value -match [regex]::Escape("[PROJECT_CREATED] project_id=$ProjectId")
}).Count -gt 0
$requiresOriginalRequest = $null -ne $meta.PSObject.Properties['source_room_id'] -and
    -not [string]::IsNullOrWhiteSpace([string]$meta.source_room_id)
$originalRequestEvent = $meta.PSObject.Properties['original_request_event_id']
$originalRequestEventId = if ($null -ne $originalRequestEvent) { [string]$originalRequestEvent.Value } else { '' }
$sourceNoticeEvent = $meta.PSObject.Properties['source_notice_event_id']
$sourceNoticeEventId = if ($null -ne $sourceNoticeEvent) { [string]$sourceNoticeEvent.Value } else { '' }
$hasOriginalRequest = @($messages.chunk | Where-Object {
    $contentProperty = $_.PSObject.Properties['content']
    if ($null -eq $contentProperty -or $null -eq $contentProperty.Value) { return $false }
    $bodyProperty = $contentProperty.Value.PSObject.Properties['body']
    return $null -ne $bodyProperty -and
        [string]$bodyProperty.Value -match [regex]::Escape("[ORIGINAL_REQUEST] project_id=$ProjectId")
}).Count -gt 0

$groups = $managerConfig.channels.matrix.groups
$roomGroup = if ($null -ne $groups) { $groups.PSObject.Properties[$roomId] } else { $null }
$managerRoomConfigured = $null -ne $roomGroup -and
    $roomGroup.Value.enabled -eq $true -and
    $roomGroup.Value.requireMention -eq $false

$remoteMetaText = (& docker exec $Controller sh -lc "mc cat hiclaw/hiclaw-storage/shared/projects/$ProjectId/meta.json" | Out-String)
$remoteMeta = $remoteMetaText | ConvertFrom-Json
$minioMatches = $remoteMeta.project_room_id -eq $roomId -and $remoteMeta.lifecycle_state -eq 'READY'

$auditCount = [int]((& docker exec $ManagerContainer sh -lc "wc -l < '/root/hiclaw-fs/shared/projects/$ProjectId/audit.ndjson'" | Out-String).Trim())
$checks = [ordered]@{
    SchemaV2 = $meta.schema_version -eq 2
    ProjectRoomOnly = $meta.interaction_mode -eq 'project_room_only'
    LifecycleReady = $meta.lifecycle_state -eq 'READY'
    AllMembersJoined = $missing.Count -eq 0
    ManagerRoomConfigured = $managerRoomConfigured
    CreatedTraceVisible = $hasCreatedTrace
    OriginalRequestRecorded = -not $requiresOriginalRequest -or
        (-not [string]::IsNullOrWhiteSpace($originalRequestEventId) -and $hasOriginalRequest)
    SourceNoticeRecorded = -not $requiresOriginalRequest -or
        -not [string]::IsNullOrWhiteSpace($sourceNoticeEventId)
    MinIOMatches = $minioMatches
    AuditTrailPresent = $auditCount -ge 3
}
$failedChecks = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object { $_.Key })
$passed = $failedChecks.Count -eq 0

$result = [pscustomobject]@{
    ProjectId = $ProjectId
    RoomId = $roomId
    RoomName = $roomName.name
    Topic = $roomTopic.topic
    LifecycleState = $meta.lifecycle_state
    InteractionMode = $meta.interaction_mode
    ExpectedMembers = $expected.Count
    JoinedMembers = $joined.Count
    MissingMembers = $missing
    ManagerRoomConfigured = $managerRoomConfigured
    CreatedTraceVisible = $hasCreatedTrace
    OriginalRequestRequired = $requiresOriginalRequest
    OriginalRequestVisible = $hasOriginalRequest
    SourceNoticeRecorded = -not [string]::IsNullOrWhiteSpace($sourceNoticeEventId)
    MinIOMatches = $minioMatches
    AuditEvents = $auditCount
    FailedChecks = $failedChecks
    Passed = $passed
}
$result | Format-List
$result | ConvertTo-Json -Depth 4 -Compress
if (-not $passed) { exit 1 }
