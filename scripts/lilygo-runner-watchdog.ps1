param(
    [string]$RunnerRoot = 'C:\actions-runner-lilygo',
    [int]$FailureWindowMinutes = 15,
    [int]$RestartCooldownMinutes = 10,
    [int]$MaxConsecutiveRestarts = 3,
    [int]$MaxLogBytes = 5242880
)

$ErrorActionPreference = 'Stop'

$stateDir = Join-Path $RunnerRoot '_watchdog'
$stateFile = Join-Path $stateDir 'state.json'
$logFile = Join-Path $stateDir 'watchdog.log'
$oldLogFile = Join-Path $stateDir 'watchdog.previous.log'
New-Item -ItemType Directory -Force -Path $stateDir | Out-Null

if ((Test-Path $logFile) -and (Get-Item $logFile).Length -ge $MaxLogBytes) {
    Remove-Item -Force -ErrorAction SilentlyContinue $oldLogFile
    Move-Item -Force $logFile $oldLogFile
}

function Write-WatchdogLog {
    param([string]$Message)
    $line = "{0:u} {1}" -f (Get-Date), $Message
    Add-Content -Path $logFile -Value $line -Encoding UTF8
}

function Read-State {
    if (-not (Test-Path $stateFile)) {
        return [pscustomobject]@{ LastRestartUtc = $null; ConsecutiveRestarts = 0 }
    }
    try {
        return (Get-Content $stateFile -Raw | ConvertFrom-Json)
    } catch {
        return [pscustomobject]@{ LastRestartUtc = $null; ConsecutiveRestarts = 0 }
    }
}

function Save-State {
    param($State)
    $State | ConvertTo-Json | Set-Content -Path $stateFile -Encoding UTF8
}

function Mark-Healthy {
    $state = Read-State
    if ([int]$state.ConsecutiveRestarts -ne 0) {
        $state.ConsecutiveRestarts = 0
        $state.LastRestartUtc = $null
        Save-State $state
    }
}

# Discover only the runner service whose executable lives under the dedicated
# LilyGO runner root. This prevents the watchdog from ever touching the Android
# MeshCore Communicator runner at C:\actions-runner.
$serviceInfo = Get-CimInstance Win32_Service | Where-Object {
    $_.PathName -like "*$RunnerRoot*RunnerService.exe*"
} | Select-Object -First 1
if (-not $serviceInfo) {
    Write-WatchdogLog 'ERROR LilyGO runner service not found.'
    exit 2
}
$service = Get-Service -Name $serviceInfo.Name -ErrorAction SilentlyContinue
if (-not $service) {
    Write-WatchdogLog 'ERROR LilyGO runner service could not be opened.'
    exit 2
}

function Get-ScopedRunnerProcess {
    param([string]$ProcessName)
    return Get-CimInstance Win32_Process -Filter "Name='$ProcessName.exe'" -ErrorAction SilentlyContinue |
        Where-Object {
            ($_.ExecutablePath -and $_.ExecutablePath.StartsWith($RunnerRoot, [StringComparison]::OrdinalIgnoreCase)) -or
            ($_.CommandLine -and $_.CommandLine -like "*$RunnerRoot*")
        } |
        Select-Object -First 1
}

# Never restart this listener while its own Worker is executing a job. A Worker
# belonging to the separate Android runner does not block LilyGO recovery.
$worker = Get-ScopedRunnerProcess 'Runner.Worker'
if ($worker) {
    Mark-Healthy
    Write-WatchdogLog "OK active LilyGO Runner.Worker PID=$($worker.ProcessId); watchdog will not intervene."
    exit 0
}

$service.Refresh()
if ($service.Status -ne 'Running') {
    Write-WatchdogLog "RECOVERY LilyGO service status is $($service.Status); starting service."
    Start-Service $service.Name
    Start-Sleep -Seconds 5
    exit 0
}

$listener = Get-ScopedRunnerProcess 'Runner.Listener'
if (-not $listener) {
    Write-WatchdogLog 'RECOVERY service says Running but scoped Runner.Listener is missing; restarting LilyGO service.'
    Restart-Service $service.Name -Force
    Start-Sleep -Seconds 5
    exit 0
}

$brokerReachable = $false
try {
    $client = New-Object System.Net.Sockets.TcpClient
    $async = $client.BeginConnect('broker.actions.githubusercontent.com', 443, $null, $null)
    if ($async.AsyncWaitHandle.WaitOne(5000, $false)) {
        $client.EndConnect($async)
        $brokerReachable = $client.Connected
    }
    $client.Close()
} catch {
    $brokerReachable = $false
}

if (-not $brokerReachable) {
    Write-WatchdogLog 'WARN GitHub Actions broker TCP 443 is unreachable; leaving LilyGO runner service alone.'
    exit 0
}

$diagDir = Join-Path $RunnerRoot '_diag'
$recentRunnerLog = Get-ChildItem $diagDir -Filter 'Runner_*.log' -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1

if (-not $recentRunnerLog) {
    Mark-Healthy
    Write-WatchdogLog 'WARN no Runner_*.log found; service/listener exist and broker is reachable.'
    exit 0
}

$cutoff = (Get-Date).ToUniversalTime().AddMinutes(-$FailureWindowMinutes)
if ($recentRunnerLog.LastWriteTimeUtc -lt $cutoff) {
    Mark-Healthy
    Write-WatchdogLog "OK listener present; broker reachable; latest LilyGO runner log is idle ($($recentRunnerLog.LastWriteTimeUtc.ToString('u')))."
    exit 0
}

$tail = @(Get-Content $recentRunnerLog.FullName -Tail 100 -ErrorAction SilentlyContinue)
$brokerHits = 0
$transportHits = 0
$transportPatterns = @(
    'SocketException',
    'timed out after 100',
    'The operation was canceled',
    'transport connection',
    'connection attempt failed'
)
foreach ($line in $tail) {
    if ($line -like '*broker.actions.githubusercontent.com*') { $brokerHits++ }
    foreach ($pattern in $transportPatterns) {
        if ($line -like "*$pattern*") {
            $transportHits++
            break
        }
    }
}

if ($brokerHits -lt 1 -or $transportHits -lt 3) {
    Mark-Healthy
    Write-WatchdogLog "OK service/listener present, broker reachable, no repeated stuck-listener signature (brokerHits=$brokerHits transportHits=$transportHits)."
    exit 0
}

$state = Read-State
if ([int]$state.ConsecutiveRestarts -ge $MaxConsecutiveRestarts) {
    Write-WatchdogLog "ERROR stuck-listener signature remains after $($state.ConsecutiveRestarts) automatic restarts; refusing further restart churn until a healthy pass resets state."
    exit 4
}

if ($state.LastRestartUtc) {
    try {
        $lastRestart = [DateTime]::Parse($state.LastRestartUtc).ToUniversalTime()
        if ($lastRestart -gt (Get-Date).ToUniversalTime().AddMinutes(-$RestartCooldownMinutes)) {
            Write-WatchdogLog 'WARN failure signature still present but restart cooldown is active; no action.'
            exit 0
        }
    } catch { }
}

Write-WatchdogLog "RECOVERY repeated stuck-listener signature detected (brokerHits=$brokerHits transportHits=$transportHits); restarting $($service.Name)."
Restart-Service $service.Name -Force
Start-Sleep -Seconds 8
$service.Refresh()

$state.LastRestartUtc = (Get-Date).ToUniversalTime().ToString('o')
$state.ConsecutiveRestarts = [int]$state.ConsecutiveRestarts + 1
Save-State $state

if ($service.Status -eq 'Running') {
    Write-WatchdogLog "RECOVERY LilyGO runner service restarted successfully (consecutive recovery count $($state.ConsecutiveRestarts)/$MaxConsecutiveRestarts)."
    exit 0
}

Write-WatchdogLog "ERROR LilyGO runner service restart finished with status $($service.Status)."
exit 3
