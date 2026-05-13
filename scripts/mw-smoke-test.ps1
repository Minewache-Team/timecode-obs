<#
.SYNOPSIS
    MW Smoke Test (TICKET-039) — Windows PowerShell variant.

.DESCRIPTION
    End-to-end exercise of the plugin↔server heartbeat contract.
    Mirrors scripts/mw-smoke-test.sh assertion-for-assertion.

    Requires:
      - PHP server running (default http://localhost:8000)
      - MariaDB/MySQL with test schema applied
        (run `php scripts/mw-smoke-helper.php install` once)

.PARAMETER BaseUrl
    Base URL of the API. Default: http://localhost:8000

.PARAMETER ApiKey
    X-API-Key header value. Default: test_api_key_12345

.PARAMETER UserName
    Test user. Default: SmokeTester

.PARAMETER CameraId
    Camera letter (A-P). Default: A

.EXAMPLE
    .\scripts\mw-smoke-test.ps1
    .\scripts\mw-smoke-test.ps1 -BaseUrl http://localhost/website -ApiKey xxx
#>

[CmdletBinding()]
param(
    [string]$BaseUrl  = $(if ($env:BASE_URL)  { $env:BASE_URL }  else { 'http://localhost:8000' }),
    [string]$ApiKey   = $(if ($env:API_KEY)   { $env:API_KEY }   else { 'test_api_key_12345' }),
    [string]$UserName = $(if ($env:USER_NAME) { $env:USER_NAME } else { 'SmokeTester' }),
    [string]$CameraId = $(if ($env:CAMERA_ID) { $env:CAMERA_ID } else { 'A' })
)

$ErrorActionPreference = 'Stop'

$script:Pass = 0
$script:Fail = 0

function Step($msg) {
    Write-Host ''
    Write-Host "[STEP] $msg" -ForegroundColor Blue
}

function Assert-Equal($got, $want, $label) {
    if ($got -eq $want) {
        Write-Host "  ✓ $label = $got" -ForegroundColor Green
        $script:Pass++
    } else {
        Write-Host "  ✗ $label`: got '$got' expected '$want'" -ForegroundColor Red
        $script:Fail++
    }
}

function Assert-True($got, $label) {
    if ($got -eq $true -or "$got" -eq 'true' -or "$got" -eq 'True') {
        Write-Host "  ✓ $label" -ForegroundColor Green
        $script:Pass++
    } else {
        Write-Host "  ✗ $label`: expected true, got '$got'" -ForegroundColor Red
        $script:Fail++
    }
}

function Assert-Missing($got, $label) {
    if ($null -eq $got -or "$got" -eq '' -or "$got" -eq 'null') {
        Write-Host "  ✓ $label (not present)" -ForegroundColor Green
        $script:Pass++
    } else {
        Write-Host "  ✗ $label`: expected missing, got '$got'" -ForegroundColor Red
        $script:Fail++
    }
}

function ApiPost($action, $body) {
    $url = "$BaseUrl/api.php?action=$action"
    return Invoke-RestMethod -Method POST -Uri $url `
        -Headers @{ 'X-API-Key' = $ApiKey; 'Content-Type' = 'application/json' } `
        -Body $body
}

function Helper {
    param([Parameter(ValueFromRemainingArguments)] [string[]]$Args)
    $script = Join-Path $PSScriptRoot 'mw-smoke-helper.php'
    & php $script @Args
}

# ---- Begin ----

Write-Host 'MW Smoke Test'
Write-Host "  BaseUrl  = $BaseUrl"
Write-Host "  ApiKey   = $ApiKey"
Write-Host "  User     = $UserName, camera $CameraId"

Step '0. Reset DB'
Helper reset | Out-Null

Step "1. POST ?action=start  (camera $CameraId)"
$resp = ApiPost 'start' (@{ name = $UserName; camera_id = $CameraId } | ConvertTo-Json -Compress)
Write-Host "  response: $($resp | ConvertTo-Json -Compress)"
Assert-True $resp.ok 'start returned ok'
if (-not $resp.session_id) {
    Write-Host '  ✗ missing session_id' -ForegroundColor Red
    $script:Fail++
}

Step '2. POST ?action=heartbeat  (recording_active=true, offset=42ms)'
$body = @{ name = $UserName; recording_active = $true; offset_ms = 42; sync_method = 1 } |
    ConvertTo-Json -Compress
$resp = ApiPost 'heartbeat' $body
Write-Host "  response: $($resp | ConvertTo-Json -Compress)"
Assert-True $resp.ok 'heartbeat returned ok'
Assert-Equal $resp.updated 1 '1 row updated'
Assert-Missing $resp.resync 'resync flag'

Step '3. Verify DB now has offset_ms = 42'
$session = Helper get_session $UserName | ConvertFrom-Json
Assert-Equal $session.offset_ms 42 'DB offset_ms'

Step '4. Heartbeat with recording_active=false (camera idle), no flag set'
$body = @{ name = $UserName; recording_active = $false; offset_ms = 15; sync_method = 1 } |
    ConvertTo-Json -Compress
$resp = ApiPost 'heartbeat' $body
Write-Host "  response: $($resp | ConvertTo-Json -Compress)"
Assert-Missing $resp.resync 'resync flag (none queued)'

Step '5. Director clicks Resync — set pending_resync=1 directly via helper'
Helper set_pending $UserName | Out-Null
$session = Helper get_session $UserName | ConvertFrom-Json
Assert-Equal $session.pending_resync 1 'DB pending_resync after set'

Step '6. Heartbeat with recording_active=true — gating: NO resync delivery'
$body = @{ name = $UserName; recording_active = $true; offset_ms = 50; sync_method = 1 } |
    ConvertTo-Json -Compress
$resp = ApiPost 'heartbeat' $body
Write-Host "  response: $($resp | ConvertTo-Json -Compress)"
Assert-Missing $resp.resync 'resync flag (gated by recording)'
$session = Helper get_session $UserName | ConvertFrom-Json
Assert-Equal $session.pending_resync 1 'DB pending_resync STILL set after gated heartbeat'

Step '7. Heartbeat with recording_active=false — resync delivered, flag cleared'
$body = @{ name = $UserName; recording_active = $false; offset_ms = 20; sync_method = 1 } |
    ConvertTo-Json -Compress
$resp = ApiPost 'heartbeat' $body
Write-Host "  response: $($resp | ConvertTo-Json -Compress)"
Assert-True $resp.resync 'resync flag delivered'
$session = Helper get_session $UserName | ConvertFrom-Json
Assert-Equal $session.pending_resync 0 'DB pending_resync cleared after delivery'

Step '8. Heartbeat again (idle) — flag stays cleared'
$body = @{ name = $UserName; recording_active = $false; offset_ms = 18; sync_method = 1 } |
    ConvertTo-Json -Compress
$resp = ApiPost 'heartbeat' $body
Assert-Missing $resp.resync 'resync flag (already consumed)'

Step '9. POST ?action=stop'
$resp = ApiPost 'stop' (@{ name = $UserName; camera_id = $CameraId } | ConvertTo-Json -Compress)
Write-Host "  response: $($resp | ConvertTo-Json -Compress)"
Assert-True $resp.ok 'stop returned ok'

Step '10. pending_resync set during idle, then new recording starts → flag must propagate'
Helper set_pending $UserName | Out-Null
$resp = ApiPost 'start' (@{ name = $UserName; camera_id = $CameraId } | ConvertTo-Json -Compress)
Write-Host "  start response: $($resp | ConvertTo-Json -Compress)"
$session = Helper get_session $UserName | ConvertFrom-Json
Assert-Equal $session.pending_resync 1 'DB pending_resync propagated to new session'

# ---- Summary ----

Write-Host ''
Write-Host '=========================================='
if ($Fail -eq 0) {
    Write-Host "PASS — $Pass assertions" -ForegroundColor Green
    exit 0
} else {
    Write-Host "FAIL — $Pass passed, $Fail failed" -ForegroundColor Red
    exit 1
}
