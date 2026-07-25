[CmdletBinding()]
param(
    [string]$ClientPath = '',
    [string]$ServerPath = '',
    [string]$Url = 'ws://127.0.0.1:7880',
    [string]$ApiKey = 'devkey',
    [string]$ApiSecret = 'secret'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Repair-ProcessPath {
    $pathVariables = @(
        [Environment]::GetEnvironmentVariables().GetEnumerator() |
            Where-Object { $_.Key -ieq 'Path' }
    )
    if ($pathVariables.Count -gt 1) {
        $canonicalPath = $env:Path
        [Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
        [Environment]::SetEnvironmentVariable('Path', $canonicalPath, 'Process')
    }
}

Repair-ProcessPath

if ([string]::IsNullOrWhiteSpace($ClientPath)) {
    $ClientPath = Join-Path $PSScriptRoot `
        '..\out\build\windows-msvc-livekit-quality-gate\apps\livekit-quality-gate\Release\hvc-livekit-quality-gate.exe'
}
if ([string]::IsNullOrWhiteSpace($ServerPath)) {
    $ServerPath = Join-Path $PSScriptRoot `
        '..\livekit_1.13.4_windows_amd64\livekit-server.exe'
}

function ConvertTo-Base64Url {
    param([byte[]]$Bytes)

    return [Convert]::ToBase64String($Bytes).TrimEnd('=').Replace('+', '-').Replace('/', '_')
}

function New-LiveKitToken {
    param(
        [string]$Identity,
        [string]$Room,
        [bool]$CanPublish,
        [bool]$CanSubscribe,
        [switch]$RoomAdmin
    )

    $now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    if ($RoomAdmin) {
        $videoGrant = [ordered]@{
            room      = $Room
            roomAdmin = $true
        }
    }
    else {
        $videoGrant = [ordered]@{
            room           = $Room
            roomJoin       = $true
            canPublish     = $CanPublish
            canSubscribe   = $CanSubscribe
            canPublishData = $false
        }
    }

    $headerJson = [ordered]@{ alg = 'HS256'; typ = 'JWT' } |
        ConvertTo-Json -Compress
    $payloadJson = [ordered]@{
        iss   = $ApiKey
        sub   = $Identity
        nbf   = $now - 5
        exp   = $now + 600
        video = $videoGrant
    } | ConvertTo-Json -Compress -Depth 5

    $header = ConvertTo-Base64Url ([Text.Encoding]::UTF8.GetBytes($headerJson))
    $payload = ConvertTo-Base64Url ([Text.Encoding]::UTF8.GetBytes($payloadJson))
    $signingInput = "$header.$payload"
    $hmac = [Security.Cryptography.HMACSHA256]::new(
        [Text.Encoding]::UTF8.GetBytes($ApiSecret)
    )
    try {
        $signature = ConvertTo-Base64Url (
            $hmac.ComputeHash([Text.Encoding]::UTF8.GetBytes($signingInput))
        )
    }
    finally {
        $hmac.Dispose()
    }

    return "$signingInput.$signature"
}

function Test-LiveKitPort {
    $client = [Net.Sockets.TcpClient]::new()
    try {
        return $client.ConnectAsync('127.0.0.1', 7880).Wait(250) -and $client.Connected
    }
    catch {
        return $false
    }
    finally {
        $client.Dispose()
    }
}

function Start-Probe {
    param([string[]]$ArgumentList)

    $identifier = [Guid]::NewGuid().ToString('N')
    $stdoutPath = Join-Path ([IO.Path]::GetTempPath()) "hvc-livekit-$identifier.stdout.log"
    $stderrPath = Join-Path ([IO.Path]::GetTempPath()) "hvc-livekit-$identifier.stderr.log"
    $process = Start-Process -FilePath $ClientPath `
        -ArgumentList $ArgumentList `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath `
        -WindowStyle Hidden `
        -PassThru

    return [pscustomobject]@{
        Process = $process
        Stdout  = $stdoutPath
        Stderr  = $stderrPath
    }
}

function Complete-Probe {
    param(
        [object]$Probe,
        [int]$TimeoutSeconds = 30
    )

    if (!$Probe.Process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $Probe.Process.Id -ErrorAction SilentlyContinue
        throw "LiveKit quality-gate process $($Probe.Process.Id) timed out."
    }
    $Probe.Process.WaitForExit()
    $stdout = Get-Content -LiteralPath $Probe.Stdout -Raw -ErrorAction SilentlyContinue
    $stderr = Get-Content -LiteralPath $Probe.Stderr -Raw -ErrorAction SilentlyContinue
    if (![string]::IsNullOrWhiteSpace($stdout)) {
        Write-Host $stdout.TrimEnd()
    }
    if (![string]::IsNullOrWhiteSpace($stderr)) {
        Write-Host $stderr.TrimEnd()
    }
    if ($stdout -notmatch '(?m)^PASS:' -or $stdout -match '(?m)^FAIL:' -or
        $stderr -match '(?m)^FAIL:') {
        throw "LiveKit quality-gate process $($Probe.Process.Id) did not report PASS."
    }
}

function Invoke-RoomService {
    param(
        [string]$Method,
        [string]$Room,
        [hashtable]$Body
    )

    $adminToken = New-LiveKitToken `
        -Identity 'hvc-quality-gate-admin' `
        -Room $Room `
        -CanPublish $false `
        -CanSubscribe $false `
        -RoomAdmin
    $httpUrl = $Url -replace '^ws:', 'http:' -replace '^wss:', 'https:'
    return Invoke-RestMethod `
        -Method Post `
        -Uri "$httpUrl/twirp/livekit.RoomService/$Method" `
        -Headers @{ Authorization = "Bearer $adminToken" } `
        -ContentType 'application/json' `
        -Body ($Body | ConvertTo-Json -Compress -Depth 5)
}

function Wait-PublishedTrack {
    param(
        [string]$Room,
        [string]$Identity,
        [int]$TimeoutSeconds = 10
    )

    $deadline = [DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTimeOffset]::UtcNow -lt $deadline) {
        $response = Invoke-RoomService `
            -Method 'ListParticipants' `
            -Room $Room `
            -Body @{ room = $Room }
        $participant = @($response.participants) |
            Where-Object { $_.identity -eq $Identity } |
            Select-Object -First 1
        if ($null -ne $participant -and @($participant.tracks).Count -gt 0) {
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Publisher '$Identity' did not expose a track before the timeout."
}

$ClientPath = [IO.Path]::GetFullPath($ClientPath)
$ServerPath = [IO.Path]::GetFullPath($ServerPath)
if (!(Test-Path -LiteralPath $ClientPath -PathType Leaf)) {
    throw "Quality-gate client not found: $ClientPath"
}

$startedServer = $null
$probes = [Collections.Generic.List[object]]::new()
try {
    if (!(Test-LiveKitPort)) {
        if (!(Test-Path -LiteralPath $ServerPath -PathType Leaf)) {
            throw "LiveKit server not found: $ServerPath"
        }
        $startedServer = Start-Process -FilePath $ServerPath `
            -ArgumentList '--dev', '--bind', '127.0.0.1' `
            -WindowStyle Hidden `
            -PassThru
        $deadline = [DateTimeOffset]::UtcNow.AddSeconds(10)
        while (!(Test-LiveKitPort) -and [DateTimeOffset]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 100
        }
        if (!(Test-LiveKitPort)) {
            throw 'The local LiveKit server did not open port 7880.'
        }
    }

    $suffix = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()

    Write-Host '=== Immediate server-side publication revocation ==='
    $revocationRoom = "hvc-revocation-$suffix"
    $publisherIdentity = "publisher-$suffix"
    $receiverToken = New-LiveKitToken `
        -Identity "receiver-$suffix" `
        -Room $revocationRoom `
        -CanPublish $false `
        -CanSubscribe $true
    $publisherToken = New-LiveKitToken `
        -Identity $publisherIdentity `
        -Room $revocationRoom `
        -CanPublish $true `
        -CanSubscribe $false
    $receiver = Start-Probe @(
        '--url', $Url,
        '--token', $receiverToken,
        '--expect-ptt',
        '--wait-for-peer', '20'
    )
    $probes.Add($receiver)
    Start-Sleep -Milliseconds 500
    $publisher = Start-Probe @(
        '--url', $Url,
        '--token', $publisherToken,
        '--publish-audio',
        '--hold', '20'
    )
    $probes.Add($publisher)
    Wait-PublishedTrack -Room $revocationRoom -Identity $publisherIdentity
    Start-Sleep -Seconds 2
    $revocationTimer = [Diagnostics.Stopwatch]::StartNew()
    $updatedPublisher = Invoke-RoomService `
        -Method 'UpdateParticipant' `
        -Room $revocationRoom `
        -Body @{
            room       = $revocationRoom
            identity   = $publisherIdentity
            permission = @{ can_publish = $false }
        }
    $revocationTimer.Stop()
    Write-Verbose ($updatedPublisher | ConvertTo-Json -Compress -Depth 5)
    if ($updatedPublisher.permission.can_publish -ne $false -or
        @($updatedPublisher.tracks).Count -ne 0 -or
        $updatedPublisher.is_publisher -ne $false) {
        throw 'LiveKit did not revoke the publisher permission and active track.'
    }
    if ($revocationTimer.ElapsedMilliseconds -gt 2000) {
        throw "LiveKit publication revocation exceeded two seconds ($($revocationTimer.ElapsedMilliseconds) ms)."
    }
    Write-Host (
        'PASS: LiveKit revoked canPublish and removed the active server-side track ' +
        "in $($revocationTimer.ElapsedMilliseconds) ms."
    )
    Complete-Probe -Probe $receiver
    if (!$publisher.Process.HasExited) {
        Stop-Process -Id $publisher.Process.Id
        Wait-Process -Id $publisher.Process.Id -ErrorAction SilentlyContinue
    }

    Write-Host '=== Track-subscription isolation ==='
    $subscriptionRoom = "hvc-subscription-$suffix"
    $blockedReceiverToken = New-LiveKitToken `
        -Identity "blocked-receiver-$suffix" `
        -Room $subscriptionRoom `
        -CanPublish $false `
        -CanSubscribe $false
    $subscriptionPublisherToken = New-LiveKitToken `
        -Identity "subscription-publisher-$suffix" `
        -Room $subscriptionRoom `
        -CanPublish $true `
        -CanSubscribe $false
    $blockedReceiver = Start-Probe @(
        '--url', $Url,
        '--token', $blockedReceiverToken,
        '--expect-no-audio',
        '--wait-for-peer', '7'
    )
    $probes.Add($blockedReceiver)
    Start-Sleep -Milliseconds 500
    $subscriptionPublisher = Start-Probe @(
        '--url', $Url,
        '--token', $subscriptionPublisherToken,
        '--publish-audio',
        '--hold', '10'
    )
    $probes.Add($subscriptionPublisher)
    Wait-PublishedTrack `
        -Room $subscriptionRoom `
        -Identity "subscription-publisher-$suffix"
    Complete-Probe -Probe $blockedReceiver
    Complete-Probe -Probe $subscriptionPublisher

    Write-Host '=== Cross-room isolation ==='
    $isolatedRoom = "hvc-isolated-$suffix"
    $foreignRoom = "hvc-foreign-$suffix"
    $isolatedToken = New-LiveKitToken `
        -Identity "isolated-receiver-$suffix" `
        -Room $isolatedRoom `
        -CanPublish $false `
        -CanSubscribe $true
    $foreignPublisherToken = New-LiveKitToken `
        -Identity "foreign-publisher-$suffix" `
        -Room $foreignRoom `
        -CanPublish $true `
        -CanSubscribe $false
    $isolatedReceiver = Start-Probe @(
        '--url', $Url,
        '--token', $isolatedToken,
        '--expect-empty-room',
        '--wait-for-peer', '7'
    )
    $probes.Add($isolatedReceiver)
    Start-Sleep -Milliseconds 500
    $foreignPublisher = Start-Probe @(
        '--url', $Url,
        '--token', $foreignPublisherToken,
        '--publish-audio',
        '--hold', '5'
    )
    $probes.Add($foreignPublisher)
    Complete-Probe -Probe $foreignPublisher
    Complete-Probe -Probe $isolatedReceiver

    Write-Host 'PASS: all LiveKit security quality-gate probes completed.'
}
finally {
    foreach ($probe in $probes) {
        if (!$probe.Process.HasExited) {
            Stop-Process -Id $probe.Process.Id -ErrorAction SilentlyContinue
            Wait-Process -Id $probe.Process.Id -ErrorAction SilentlyContinue
        }
        Remove-Item -LiteralPath $probe.Stdout -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $probe.Stderr -Force -ErrorAction SilentlyContinue
    }
    if ($null -ne $startedServer -and !$startedServer.HasExited) {
        Stop-Process -Id $startedServer.Id
        Wait-Process -Id $startedServer.Id -ErrorAction SilentlyContinue
    }
}
