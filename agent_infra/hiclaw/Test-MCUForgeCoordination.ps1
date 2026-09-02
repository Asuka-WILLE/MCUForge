param(
    [string]$Controller = "hiclaw-controller",
    [string]$ManagerContainer = "hiclaw-manager",
    [string]$TeamName = "mcuforge",
    [int]$GatewayPort = 18080,
    [ValidateRange(1, 20)]
    [int]$Rounds = 3,
    [ValidateRange(10, 180)]
    [int]$AckTimeoutSeconds = 60,
    [ValidateRange(10, 180)]
    [int]$ManagerTimeoutSeconds = 60
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-MatrixMessages {
    param(
        [string]$BaseUri,
        [hashtable]$Headers,
        [string]$RoomId
    )
    $escapedRoom = [uri]::EscapeDataString($RoomId)
    return @(Invoke-RestMethod -Method Get `
        -Uri "$BaseUri/_matrix/client/v3/rooms/$escapedRoom/messages?dir=b&limit=100" `
        -Headers $Headers).chunk
}

function Get-RelationType {
    param($Event)
    if ($null -eq $Event -or $null -eq $Event.content) {
        return $null
    }
    $relationProperty = $Event.content.PSObject.Properties['m.relates_to']
    if ($null -eq $relationProperty -or $null -eq $relationProperty.Value) {
        return $null
    }
    $typeProperty = $relationProperty.Value.PSObject.Properties['rel_type']
    if ($null -eq $typeProperty) {
        return $null
    }
    return $typeProperty.Value
}

function Get-EventBody {
    param($Event)
    if ($null -eq $Event -or $null -eq $Event.content) {
        return ''
    }
    $bodyProperty = $Event.content.PSObject.Properties['body']
    if ($null -eq $bodyProperty) {
        return ''
    }
    return [string]$bodyProperty.Value
}

function Get-MentionUserIds {
    param($Event)
    if ($null -eq $Event -or $null -eq $Event.content) {
        return @()
    }
    $mentionsProperty = $Event.content.PSObject.Properties['m.mentions']
    if ($null -eq $mentionsProperty -or $null -eq $mentionsProperty.Value) {
        return @()
    }
    $usersProperty = $mentionsProperty.Value.PSObject.Properties['user_ids']
    if ($null -eq $usersProperty) {
        return @()
    }
    return @($usersProperty.Value)
}

$managerConfigText = (& docker exec $ManagerContainer cat /root/manager-workspace/openclaw.json | Out-String)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($managerConfigText)) {
    throw "Manager Matrix config is not readable"
}
$managerConfig = $managerConfigText | ConvertFrom-Json
$managerId = [string]$managerConfig.channels.matrix.userId
$managerToken = [string]$managerConfig.channels.matrix.accessToken
if ([string]::IsNullOrWhiteSpace($managerId) -or [string]::IsNullOrWhiteSpace($managerToken)) {
    throw "Manager Matrix identity or token is missing"
}
$domain = $managerId.Substring($managerId.IndexOf(':') + 1)
$headers = @{ Authorization = "Bearer $managerToken" }
$matrixBase = "http://127.0.0.1:$GatewayPort"

$workerDataText = (& docker exec $Controller hiclaw get workers --team $TeamName -o json | Out-String)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($workerDataText)) {
    throw "Cannot query Team workers"
}
$workers = @((ConvertFrom-Json $workerDataText).workers | Where-Object { $_.role -eq 'worker' })
if ($workers.Count -lt 1) {
    throw "Team has no Worker rooms: $TeamName"
}

$results = [System.Collections.Generic.List[object]]::new()
for ($round = 1; $round -le $Rounds; $round++) {
    $pending = [System.Collections.Generic.List[object]]::new()
    foreach ($worker in $workers) {
        $workerName = [string]$worker.name
        $workerId = "@$workerName`:$domain"
        $roomId = [string]$worker.roomID
        if ([string]::IsNullOrWhiteSpace($roomId)) {
            throw "Worker room is missing: $workerName"
        }
        $token = "COORD_R${round}_$($workerName.Replace('mcuforge-', '').ToUpperInvariant())_$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())"
        $sentAt = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
        $workerLink = "https://matrix.to/#/$([uri]::EscapeDataString($workerId))"
        $managerLink = "https://matrix.to/#/$([uri]::EscapeDataString($managerId))"
        $body = "@$workerName 协调链路测试：$token。请只回复并明确提及 $managerId：TASK_RECEIVED: $token。"
        $formattedBody = "<a href=`"$workerLink`">$workerId</a> 协调链路测试：$token。请只回复并明确提及 <a href=`"$managerLink`">$managerId</a>：TASK_RECEIVED: $token。"
        $payload = @{
            msgtype = 'm.text'
            body = $body
            format = 'org.matrix.custom.html'
            formatted_body = $formattedBody
            'm.mentions' = @{ user_ids = @($workerId) }
        } | ConvertTo-Json -Depth 6 -Compress
        $escapedRoom = [uri]::EscapeDataString($roomId)
        $transactionId = [guid]::NewGuid().ToString('N')
        Invoke-RestMethod -Method Put `
            -Uri "$matrixBase/_matrix/client/v3/rooms/$escapedRoom/send/m.room.message/$transactionId" `
            -Headers $headers `
            -ContentType 'application/json' `
            -Body $payload | Out-Null
        $pending.Add([pscustomobject]@{
            Round = $round
            Worker = $workerName
            WorkerId = $workerId
            RoomId = $roomId
            Token = $token
            SentAt = $sentAt
            Ack = $null
            ManagerFollowup = $null
        })
    }

    $ackDeadline = [DateTime]::UtcNow.AddSeconds($AckTimeoutSeconds)
    do {
        foreach ($item in @($pending | Where-Object { $null -eq $_.Ack })) {
            $messages = Get-MatrixMessages -BaseUri $matrixBase -Headers $headers -RoomId $item.RoomId
            $item.Ack = @($messages | Where-Object {
                $_.sender -eq $item.WorkerId -and (Get-EventBody -Event $_) -like "*$($item.Token)*"
            }) | Select-Object -First 1
        }
        if (@($pending | Where-Object { $null -eq $_.Ack }).Count -gt 0) {
            Start-Sleep -Seconds 1
        }
    } while (@($pending | Where-Object { $null -eq $_.Ack }).Count -gt 0 -and [DateTime]::UtcNow -lt $ackDeadline)

    $managerDeadline = [DateTime]::UtcNow.AddSeconds($ManagerTimeoutSeconds)
    do {
        foreach ($item in @($pending | Where-Object { $null -ne $_.Ack -and $null -eq $_.ManagerFollowup })) {
            $messages = Get-MatrixMessages -BaseUri $matrixBase -Headers $headers -RoomId $item.RoomId
            $item.ManagerFollowup = @($messages | Where-Object {
                $_.sender -eq $managerId -and
                [int64]$_.origin_server_ts -gt [int64]$item.Ack.origin_server_ts -and
                (Get-EventBody -Event $_) -like "*$($item.Token)*"
            }) | Sort-Object origin_server_ts | Select-Object -First 1
        }
        if (@($pending | Where-Object { $null -ne $_.Ack -and $null -eq $_.ManagerFollowup }).Count -gt 0) {
            Start-Sleep -Seconds 1
        }
    } while (@($pending | Where-Object { $null -ne $_.Ack -and $null -eq $_.ManagerFollowup }).Count -gt 0 -and [DateTime]::UtcNow -lt $managerDeadline)

    foreach ($item in $pending) {
        $ackRelation = Get-RelationType -Event $item.Ack
        $managerRelation = Get-RelationType -Event $item.ManagerFollowup
        $ackMentionsManager = $null -ne $item.Ack -and
            (@(Get-MentionUserIds -Event $item.Ack) -contains $managerId)
        $passed = $null -ne $item.Ack -and
            $null -ne $item.ManagerFollowup -and
            $null -eq $ackRelation -and
            $null -eq $managerRelation -and
            $ackMentionsManager
        $results.Add([pscustomobject]@{
            Round = $item.Round
            Worker = $item.Worker
            Ack = $null -ne $item.Ack
            AckLatencyMs = if ($null -ne $item.Ack) { [int64]$item.Ack.origin_server_ts - [int64]$item.SentAt } else { $null }
            AckIsFinalEvent = $null -ne $item.Ack -and $null -eq $ackRelation
            AckMentionsManager = $ackMentionsManager
            ManagerFollowup = $null -ne $item.ManagerFollowup
            ManagerLatencyMs = if ($null -ne $item.ManagerFollowup) { [int64]$item.ManagerFollowup.origin_server_ts - [int64]$item.Ack.origin_server_ts } else { $null }
            ManagerFollowupIsFinalEvent = $null -ne $item.ManagerFollowup -and $null -eq $managerRelation
            Passed = $passed
        })
    }
}

$results | Format-Table -AutoSize
$failed = @($results | Where-Object { -not $_.Passed })
$summary = [pscustomobject]@{
    Team = $TeamName
    Rounds = $Rounds
    Checks = $results.Count
    Passed = $results.Count - $failed.Count
    Failed = $failed.Count
    AckLatencyMaxMs = ($results | Measure-Object -Property AckLatencyMs -Maximum).Maximum
    ManagerLatencyMaxMs = ($results | Measure-Object -Property ManagerLatencyMs -Maximum).Maximum
}
$summary | ConvertTo-Json -Compress
if ($failed.Count -gt 0) {
    throw "MCUForge coordination regression failed: $($failed.Count)/$($results.Count)"
}
