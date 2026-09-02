param(
    [Parameter(Mandatory)]
    [ValidateNotNullOrEmpty()]
    [string[]]$ProjectId,
    [string]$Reason = "validation room no longer needed",
    [string]$ManagerContainer = "hiclaw-manager",
    [string]$Controller = "hiclaw-controller",
    [string]$HiClawEnvPath = (Join-Path $env:USERPROFILE "hiclaw-manager.env"),
    [switch]$Execute
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if (-not (Test-Path -LiteralPath $HiClawEnvPath -PathType Leaf)) {
    throw "HiClaw environment file not found: $HiClawEnvPath"
}

$settings = @{}
Get-Content -LiteralPath $HiClawEnvPath | Where-Object { $_ -match '^[A-Z0-9_]+=' } | ForEach-Object {
    $key, $value = $_.Split('=', 2)
    $settings[$key] = $value
}
foreach ($required in @('HICLAW_ADMIN_USER', 'HICLAW_ADMIN_PASSWORD', 'HICLAW_PORT_GATEWAY')) {
    if ([string]::IsNullOrWhiteSpace($settings[$required])) {
        throw "Missing $required in $HiClawEnvPath"
    }
}

$matrixBase = "http://127.0.0.1:$($settings['HICLAW_PORT_GATEWAY'])"
$adminLoginBody = @{
    type = 'm.login.password'
    identifier = @{ type = 'm.id.user'; user = $settings['HICLAW_ADMIN_USER'] }
    password = $settings['HICLAW_ADMIN_PASSWORD']
} | ConvertTo-Json -Compress
$adminLogin = Invoke-RestMethod -Method Post -Uri "$matrixBase/_matrix/client/v3/login" -ContentType 'application/json' -Body $adminLoginBody
$adminId = [string]$adminLogin.user_id
$adminHeaders = @{ Authorization = "Bearer $($adminLogin.access_token)" }

$managerConfigText = (& docker exec $ManagerContainer cat /root/manager-workspace/openclaw.json | Out-String)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($managerConfigText)) {
    throw "Manager Matrix config is not readable"
}
$managerConfig = $managerConfigText | ConvertFrom-Json
$managerId = [string]$managerConfig.channels.matrix.userId
$managerHeaders = @{ Authorization = "Bearer $($managerConfig.channels.matrix.accessToken)" }
$storagePrefix = (& docker exec $Controller printenv HICLAW_STORAGE_PREFIX | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($storagePrefix)) {
    throw "HiClaw storage prefix is unavailable"
}

function Invoke-MatrixBestEffort {
    param(
        [ValidateSet('Post', 'Put', 'Delete')]
        [string]$Method,
        [string]$Uri,
        [hashtable]$Headers,
        [string]$Body
    )
    try {
        $parameters = @{ Method = $Method; Uri = $Uri; Headers = $Headers }
        if (-not [string]::IsNullOrWhiteSpace($Body)) {
            $parameters.ContentType = 'application/json'
            $parameters.Body = $Body
        }
        Invoke-RestMethod @parameters | Out-Null
        return $true
    }
    catch {
        return $false
    }
}

$inventory = @()
foreach ($id in $ProjectId) {
    if ($id -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{2,79}$') {
        throw "Invalid project id: $id"
    }
    $metaPath = "/root/hiclaw-fs/shared/projects/$id/meta.json"
    $metaText = (& docker exec $ManagerContainer cat $metaPath | Out-String)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($metaText)) {
        throw "Project metadata not found: $id"
    }
    $meta = $metaText | ConvertFrom-Json
    if ([string]$meta.project_id -ne $id) {
        throw "Project metadata identity mismatch: $id"
    }
    $roomId = [string]$meta.project_room_id
    if ([string]::IsNullOrWhiteSpace($roomId)) {
        throw "Project room id is missing: $id"
    }
    $escapedRoom = [uri]::EscapeDataString($roomId)
    $members = Invoke-RestMethod -Method Get -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedRoom/joined_members" -Headers $managerHeaders
    $joined = @($members.joined.PSObject.Properties.Name)
    $expected = @($meta.participants)
    $unknown = @($joined | Where-Object { $_ -notin $expected })
    if ($unknown.Count -gt 0) {
        throw "Refusing to terminate $id because unexpected members are present: $($unknown -join ', ')"
    }
    $inventory += [pscustomobject]@{
        ProjectId = $id
        Title = [string]$meta.title
        RoomId = $roomId
        LifecycleState = [string]$meta.lifecycle_state
        JoinedMembers = $joined
        Meta = $meta
        MetaPath = $metaPath
    }
}

if (-not $Execute) {
    $inventory | Select-Object ProjectId, Title, RoomId, LifecycleState, @{ Name = 'MemberCount'; Expression = { $_.JoinedMembers.Count } } | Format-Table -AutoSize
    [pscustomobject]@{
        mode = 'dry-run'
        projects = $inventory.Count
        action = 'Re-run with -Execute to audit, close membership, leave and forget these exact rooms.'
    } | ConvertTo-Json -Compress
    exit 0
}

$results = @()
$terminatedRoomIds = [System.Collections.Generic.List[string]]::new()
foreach ($item in $inventory) {
    $id = $item.ProjectId
    $roomId = $item.RoomId
    $escapedRoom = [uri]::EscapeDataString($roomId)
    $now = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    $notice = "[PROJECT_TERMINATED] project_id=$id terminated_at=$now reason=$Reason。此验收房间已停止使用，项目证据保留在 MinIO。"
    $txn = "mcuforge-terminate-$id-$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
    $noticeBody = @{
        msgtype = 'm.text'
        body = $notice
        'm.mentions' = @{ user_ids = @($adminId) }
    } | ConvertTo-Json -Compress -Depth 5
    $noticeSent = Invoke-MatrixBestEffort -Method Put -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedRoom/send/m.room.message/$txn" -Headers $managerHeaders -Body $noticeBody
    if (-not $noticeSent) {
        throw "Unable to write termination notice: $id"
    }

    $terminatedName = "已终止: $($item.Title)"
    $renamed = Invoke-MatrixBestEffort -Method Put -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedRoom/state/m.room.name" -Headers $managerHeaders -Body (@{ name = $terminatedName } | ConvertTo-Json -Compress)
    if (-not $renamed) {
        throw "Unable to mark room name as terminated: $id"
    }

    $meta = $item.Meta
    $meta | Add-Member -NotePropertyName status -NotePropertyValue 'terminated' -Force
    $meta | Add-Member -NotePropertyName lifecycle_state -NotePropertyValue 'TERMINATED' -Force
    $meta | Add-Member -NotePropertyName terminated_at -NotePropertyValue $now -Force
    $meta | Add-Member -NotePropertyName termination_reason -NotePropertyValue $Reason -Force
    $meta | Add-Member -NotePropertyName updated_at -NotePropertyValue $now -Force

    $auditText = (& docker exec $ManagerContainer sh -lc "cat '/root/hiclaw-fs/shared/projects/$id/audit.ndjson' 2>/dev/null || true" | Out-String)
    $auditLine = [ordered]@{
        ts = $now
        project_id = $id
        state = 'TERMINATED'
        detail = $Reason
        room_id = $roomId
    } | ConvertTo-Json -Compress
    $metaTemp = New-TemporaryFile
    $auditTemp = New-TemporaryFile
    try {
        [System.IO.File]::WriteAllText($metaTemp.FullName, ($meta | ConvertTo-Json -Depth 20), [System.Text.UTF8Encoding]::new($false))
        [System.IO.File]::WriteAllText($auditTemp.FullName, $auditText.TrimEnd() + "`n" + $auditLine + "`n", [System.Text.UTF8Encoding]::new($false))
        & docker cp $metaTemp.FullName "${ManagerContainer}:$($item.MetaPath)" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to persist terminated metadata: $id" }
        & docker cp $auditTemp.FullName "${ManagerContainer}:/root/hiclaw-fs/shared/projects/$id/audit.ndjson" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Unable to persist termination audit: $id" }
    }
    finally {
        Remove-Item -LiteralPath $metaTemp.FullName -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $auditTemp.FullName -Force -ErrorAction SilentlyContinue
    }

    & docker exec $ManagerContainer mc mirror "/root/hiclaw-fs/shared/projects/$id/" "$storagePrefix/shared/projects/$id/" --overwrite | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to sync terminated project evidence: $id"
    }

    $roomAliasProperty = $meta.PSObject.Properties['room_alias']
    if ($null -ne $roomAliasProperty -and -not [string]::IsNullOrWhiteSpace([string]$roomAliasProperty.Value)) {
        $escapedAlias = [uri]::EscapeDataString([string]$roomAliasProperty.Value)
        [void](Invoke-MatrixBestEffort -Method Delete -Uri "$matrixBase/_matrix/client/v3/directory/room/$escapedAlias" -Headers $adminHeaders -Body '')
    }

    foreach ($member in $item.JoinedMembers) {
        if ($member -ieq $managerId -or $member -ieq $adminId) { continue }
        $kickBody = @{ user_id = $member; reason = "Project terminated: $Reason" } | ConvertTo-Json -Compress
        if (-not (Invoke-MatrixBestEffort -Method Post -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedRoom/kick" -Headers $managerHeaders -Body $kickBody)) {
            throw "Unable to remove $member from terminated project $id"
        }
    }

    if ($item.JoinedMembers -contains $managerId) {
        if (-not (Invoke-MatrixBestEffort -Method Post -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedRoom/leave" -Headers $managerHeaders -Body '{}')) {
            throw "Manager failed to leave terminated project: $id"
        }
        [void](Invoke-MatrixBestEffort -Method Post -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedRoom/forget" -Headers $managerHeaders -Body '{}')
    }
    if ($item.JoinedMembers -contains $adminId) {
        if (-not (Invoke-MatrixBestEffort -Method Post -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedRoom/leave" -Headers $adminHeaders -Body '{}')) {
            throw "Admin failed to leave terminated project: $id"
        }
        [void](Invoke-MatrixBestEffort -Method Post -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedRoom/forget" -Headers $adminHeaders -Body '{}')
    }

    [void]$terminatedRoomIds.Add($roomId)
    $results += [pscustomobject]@{
        ProjectId = $id
        RoomId = $roomId
        Title = $item.Title
        RemovedAgents = @($item.JoinedMembers | Where-Object { $_ -ine $managerId -and $_ -ine $adminId }).Count
        EvidenceRetained = $true
        Status = 'TERMINATED'
    }
}

$roomIdsJson = @($terminatedRoomIds) | ConvertTo-Json -Compress
$configCleanup = @'
set -eu
rooms='__ROOMS__'
for config in /root/manager-workspace/openclaw.json /root/manager-workspace/.openclaw/openclaw.json /root/hiclaw-fs/agents/manager/openclaw.json; do
  [ -f "$config" ] || continue
  jq --argjson rooms "$rooms" 'reduce $rooms[] as $room (. ; del(.channels.matrix.groups[$room]))' "$config" > /tmp/mcuforge-manager-room-cleanup.json
  mv /tmp/mcuforge-manager-room-cleanup.json "$config"
done
mc cp /root/manager-workspace/openclaw.json '__STORAGE__/manager/openclaw.json' >/dev/null
'@.Replace('__ROOMS__', $roomIdsJson).Replace('__STORAGE__', $storagePrefix)
& docker exec $ManagerContainer sh -lc $configCleanup | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Unable to remove terminated rooms from Manager config"
}

$joinedAfter = @(Invoke-RestMethod -Method Get -Uri "$matrixBase/_matrix/client/v3/joined_rooms" -Headers $adminHeaders).joined_rooms
$stillJoined = @($terminatedRoomIds | Where-Object { $_ -in $joinedAfter })
if ($stillJoined.Count -gt 0) {
    throw "Admin still belongs to terminated rooms: $($stillJoined -join ', ')"
}

$results | Format-Table -AutoSize
[pscustomobject]@{
    mode = 'executed'
    terminated = $results.Count
    admin_rooms_removed = $stillJoined.Count -eq 0
    evidence_retained = $true
    projects = $results
} | ConvertTo-Json -Depth 5 -Compress
