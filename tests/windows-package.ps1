param(
    [Parameter(Mandatory=$true)][string]$Installer,
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
# Explicit installed-package acceptance. Run elevated in the interactive session.
# Upgrades this installation and enables sharing, then checks ordinary relaunches.
$ErrorActionPreference = 'Stop'
if ((Get-Process -Id $PID).SessionId -eq 0) { throw 'Interactive session required' }
$root = (New-Item -ItemType Directory -Force $OutputDirectory).FullName
$install = Join-Path $env:ProgramFiles 'DeskPort'
$started = Get-Date
Start-Transcript (Join-Path $root 'package-test.log')
try {
    $status = Get-MpComputerStatus
    $preferences = Get-MpPreference
    if (!$status.RealTimeProtectionEnabled -or $preferences.DisableRealtimeMonitoring -or
        $preferences.ExclusionPath -or $preferences.ExclusionProcess -or $preferences.ExclusionExtension) {
        throw 'Defender protection/exclusion preflight failed'
    }
    Get-FileHash $Installer | Format-List
    $setup = Start-Process $Installer -ArgumentList '/S' -Wait -PassThru
    if ($setup.ExitCode -ne 0) { throw "Installer failed: $($setup.ExitCode)" }
    $exe = Join-Path $install 'DeskPort.exe'
    & (Join-Path $PSScriptRoot 'windows-cli.ps1') -Executable $exe -Version $Version
    $registered = Get-ItemProperty 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\DeskPort'
    if ($registered.DisplayVersion -ne $Version) { throw 'Installed version mismatch' }
    foreach ($round in 1..3) {
        if ($round -eq 1) { Start-Process $exe -ArgumentList '--share' }
        else { Start-Process $exe }
        $deadline = (Get-Date).AddSeconds(90)
        $ready = $false
        do {
            Start-Sleep -Seconds 2
            $hostProcess = Get-CimInstance Win32_Process | Where-Object {
                $_.ExecutablePath -eq "$install\host\deskport-host.exe"
            } | Select-Object -First 1
            if ($hostProcess) {
                $port = (Get-ItemProperty 'HKCU:\Software\DeskPort\DeskPort\host').port
                try {
                    $response = Invoke-WebRequest "http://127.0.0.1:$port/serverinfo" -UseBasicParsing -TimeoutSec 3
                    # Windows PowerShell returns byte[] for this XML content type.
                    $body = if ($response.Content -is [byte[]]) {
                        [Text.Encoding]::UTF8.GetString($response.Content)
                    } else { [string]$response.Content }
                    $ready = $response.StatusCode -eq 200 -and $body -match '<hostname>'
                } catch { $ready = $false }
            }
        } while (!$ready -and (Get-Date) -lt $deadline)
        if (!$ready) { throw "Round ${round}: host did not become ready" }
        Start-Sleep -Seconds 60
        Start-MpScan -ScanType CustomScan -ScanPath $install
        if (Get-MpThreatDetection | Where-Object { $_.InitialDetectionTime -ge $started }) {
            throw 'New Defender detection; stopping without restoring or excluding anything'
        }
        foreach ($name in 'DeskPort','deskport-host','deskport-display','deskport-display-recovery') {
            if (!(Get-Process $name -ErrorAction SilentlyContinue)) { throw "Missing process: $name" }
        }
        $response = Invoke-WebRequest "http://127.0.0.1:$port/serverinfo" -UseBasicParsing -TimeoutSec 3
        if ($response.StatusCode -ne 200) { throw 'Host stopped responding' }
        "PASS: round $round; host ready; version $Version; Defender enabled; no new detection" |
            Tee-Object -FilePath (Join-Path $root 'rounds.txt') -Append
        if ($round -lt 3) {
            & "$install\deskport-maintenance.exe" stop $install
            if ($LASTEXITCODE -ne 0) { throw 'Application cleanup failed' }
            & "$install\host\deskport-display-recovery.exe" inspect
            if ($LASTEXITCODE -ne 0) { throw 'Display was not restored before relaunch' }
        }
    }
    'PASS' | Set-Content (Join-Path $root 'result.txt')
} catch {
    $_ | Out-String | Set-Content (Join-Path $root 'result.txt')
    throw
} finally { Stop-Transcript }
