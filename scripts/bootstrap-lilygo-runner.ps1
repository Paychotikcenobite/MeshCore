param(
    [Parameter(Mandatory = $true)]
    [string]$RegistrationToken,
    [string]$RepoUrl = 'https://github.com/Paychotikcenobite/MeshCore',
    [string]$RunnerRoot = 'C:\actions-runner-lilygo',
    [string]$RunnerName = 'lilygo-builder',
    [string]$RunnerLabel = 'lilygo',
    [string]$TaskName = 'LilyGo-Runner-Watchdog'
)

$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an Administrator PowerShell window on LEVIATHAN.'
}

if ($RunnerRoot -ieq 'C:\actions-runner') {
    throw 'Refusing to use the Android runner root. LilyGO must remain a separate repository-level runner.'
}

New-Item -ItemType Directory -Force -Path $RunnerRoot | Out-Null
$toolCache = Join-Path $RunnerRoot '_toolcache'
New-Item -ItemType Directory -Force -Path $toolCache | Out-Null

# Register a second, repository-specific runner. The existing Android runner at
# C:\actions-runner is intentionally untouched.
$config = Join-Path $RunnerRoot 'config.cmd'
if (-not (Test-Path $config)) {
    $release = Invoke-RestMethod -Headers @{ 'User-Agent' = 'meshcore-lilygo-runner-bootstrap' } -Uri 'https://api.github.com/repos/actions/runner/releases/latest'
    $asset = $release.assets | Where-Object { $_.name -like 'actions-runner-win-x64-*.zip' } | Select-Object -First 1
    if (-not $asset) { throw 'Unable to locate the current Windows x64 GitHub Actions runner package.' }
    $runnerZip = Join-Path $env:TEMP $asset.name
    Invoke-WebRequest -UseBasicParsing -Uri $asset.browser_download_url -OutFile $runnerZip
    Expand-Archive -Path $runnerZip -DestinationPath $RunnerRoot -Force
    Remove-Item -Force $runnerZip
}

$runnerMarker = Join-Path $RunnerRoot '.runner'
if (-not (Test-Path $runnerMarker)) {
    Push-Location $RunnerRoot
    try {
        & .\config.cmd --unattended --url $RepoUrl --token $RegistrationToken --name $RunnerName --labels $RunnerLabel --work '_work' --runasservice
        if ($LASTEXITCODE -ne 0) { throw "GitHub runner registration failed with exit code $LASTEXITCODE." }
    } finally {
        Pop-Location
    }
} else {
    Write-Host 'Existing LilyGO runner registration found; registration step skipped.'
}

$service = Get-CimInstance Win32_Service | Where-Object {
    $_.PathName -like "*$RunnerRoot*RunnerService.exe*"
} | Select-Object -First 1
if (-not $service) {
    throw "LilyGO runner Windows service was not found under $RunnerRoot."
}

Set-Service -Name $service.Name -StartupType Automatic
& sc.exe failure $service.Name 'reset= 86400' 'actions= restart/60000/restart/120000/restart/300000' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Failed to configure service recovery actions.' }
& sc.exe failureflag $service.Name 1 | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Failed to enable service recovery for non-crash failures.' }

# Reuse the already-proven PortableGit from the Android builder. This avoids a
# second Git installation and does not modify the Android runner.
$sharedBash = 'C:\actions-runner\_toolcache\portable-git\bin\bash.exe'
if (-not (Test-Path $sharedBash)) {
    $systemBash = 'C:\Program Files\Git\bin\bash.exe'
    if (-not (Test-Path $systemBash)) {
        throw 'Git Bash was not found. The verified Android PortableGit cache was expected at C:\actions-runner\_toolcache\portable-git.'
    }
}

# Install a persistent Python + PlatformIO environment inside the LilyGO runner
# root. uv is used only to bootstrap the toolchain; subsequent builds reuse it.
$uvDir = Join-Path $toolCache 'uv'
$uvExe = Join-Path $uvDir 'uv.exe'
if (-not (Test-Path $uvExe)) {
    New-Item -ItemType Directory -Force -Path $uvDir | Out-Null
    $uvRelease = Invoke-RestMethod -Headers @{ 'User-Agent' = 'meshcore-lilygo-runner-bootstrap' } -Uri 'https://api.github.com/repos/astral-sh/uv/releases/latest'
    $uvAsset = $uvRelease.assets | Where-Object { $_.name -eq 'uv-x86_64-pc-windows-msvc.zip' } | Select-Object -First 1
    if (-not $uvAsset) { throw 'Unable to locate the current Windows x64 uv package.' }
    $uvZip = Join-Path $env:TEMP $uvAsset.name
    Invoke-WebRequest -UseBasicParsing -Uri $uvAsset.browser_download_url -OutFile $uvZip
    Expand-Archive -Path $uvZip -DestinationPath $uvDir -Force
    Remove-Item -Force $uvZip
}

$pioVenv = Join-Path $toolCache 'platformio'
$pioExe = Join-Path $pioVenv 'Scripts\pio.exe'
if (-not (Test-Path $pioExe)) {
    & $uvExe python install 3.13
    if ($LASTEXITCODE -ne 0) { throw 'uv failed to install Python 3.13.' }
    & $uvExe venv $pioVenv --python 3.13
    if ($LASTEXITCODE -ne 0) { throw 'uv failed to create the PlatformIO virtual environment.' }
    & $uvExe pip install --python (Join-Path $pioVenv 'Scripts\python.exe') platformio
    if ($LASTEXITCODE -ne 0) { throw 'PlatformIO installation failed.' }
}
& $pioExe --version
if ($LASTEXITCODE -ne 0) { throw 'PlatformIO verification failed.' }

# Install the hardened watchdog from the public communicator-compact branch.
$watchdogDir = Join-Path $RunnerRoot '_watchdog'
$watchdogPath = Join-Path $watchdogDir 'lilygo-runner-watchdog.ps1'
New-Item -ItemType Directory -Force -Path $watchdogDir | Out-Null
$watchdogUrl = 'https://raw.githubusercontent.com/Paychotikcenobite/MeshCore/communicator-compact/scripts/lilygo-runner-watchdog.ps1'
Invoke-WebRequest -UseBasicParsing -Uri $watchdogUrl -OutFile $watchdogPath

$taskAction = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$watchdogPath`" -RunnerRoot `"$RunnerRoot`""
$startupTrigger = New-ScheduledTaskTrigger -AtStartup
$repeatTrigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(1) -RepetitionInterval (New-TimeSpan -Minutes 5) -RepetitionDuration (New-TimeSpan -Days 3650)
$taskPrincipal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
Register-ScheduledTask -TaskName $TaskName -Action $taskAction -Trigger @($startupTrigger, $repeatTrigger) -Principal $taskPrincipal -Force | Out-Null

# Keep LEVIATHAN awake on AC power just as the Android runner hardening does.
& powercfg.exe /change standby-timeout-ac 0
if ($LASTEXITCODE -ne 0) { throw 'Failed to disable AC standby timeout.' }

$serviceNow = Get-Service -Name $service.Name
if ($serviceNow.Status -ne 'Running') {
    Start-Service $service.Name
}
Start-Sleep -Seconds 5
$serviceNow = Get-Service -Name $service.Name

Write-Host ''
Write-Host 'LilyGO self-hosted runner setup VERIFIED.'
Write-Host "Service: $($service.Name)"
Write-Host "Service status: $($serviceNow.Status)"
Write-Host 'Service startup: Automatic'
Write-Host 'Service recovery: 60s, 120s, 300s'
Write-Host "Runner root: $RunnerRoot"
Write-Host "Runner name: $RunnerName"
Write-Host "Runner label: $RunnerLabel"
Write-Host "PlatformIO: $pioExe"
Write-Host "Watchdog task: $TaskName"
Write-Host "Watchdog cadence: startup + every 5 minutes"
Write-Host 'AC standby while plugged in: disabled'
Write-Host ''
Write-Host 'The Android runner at C:\actions-runner was not reconfigured or modified.'
