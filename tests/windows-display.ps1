param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateRange(1, 100)][int]$Rounds = 1
)

# Explicit native acceptance: temporarily enables only DeskPort's owned VDD.
# Run elevated in the interactive console session, not directly in SSH/session 0.
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path $Executable).Path
$OutputDirectory = (New-Item -ItemType Directory -Force $OutputDirectory).FullName
$recovery = Join-Path (Split-Path $Executable) 'deskport-display-recovery.exe'
if ((Get-Process -Id $PID).SessionId -eq 0) { throw 'Interactive session required.' }
if (Get-Process deskport-display -ErrorAction SilentlyContinue) { throw 'Stop existing sharing before this test.' }

function Snapshot {
    $lines = & $recovery inspect
    if ($LASTEXITCODE -ne 0) { throw "Owned display is not disabled at rest: $lines" }
    return ($lines -join "`n")
}
function Read-Reply($process) {
    $pending = $process.StandardOutput.ReadLineAsync()
    if (-not $pending.Wait(25000)) { throw 'Display response timed out.' }
    if (-not $pending.Result) { throw 'Display helper exited before replying.' }
    $reply = $pending.Result | ConvertFrom-Json
    if ($reply.error) { throw $reply.error }
    return $reply
}

$baseline = Snapshot
$baseline | Set-Content (Join-Path $OutputDirectory 'before.txt')
$results = @()
$crashState = $null
for ($round = 1; $round -le $Rounds; $round++) {
foreach ($scenario in @('resize-release', 'forced-exit', 'restart-after-crash')) {
    $state = if ($scenario -eq 'restart-after-crash') { $crashState } else { Join-Path $OutputDirectory ($scenario + '-' + [guid]::NewGuid()) }
    New-Item -ItemType Directory -Force -Path $state | Out-Null
    if ($scenario -eq 'forced-exit') { $crashState = $state }
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $Executable
    $start.Arguments = '1920 1080'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.EnvironmentVariables['DESKPORT_DISPLAY_STATE_DIR'] = $state
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    $stderr = $null
    try {
        [void]$process.Start()
        $stderr = $process.StandardError.ReadToEndAsync()
        $ready = Read-Reply $process
        if (-not $ready.virtual) { throw 'Test requires the owned virtual display.' }
        $mode = $ready.displayModes | Where-Object { $_.width -eq 1920 -and $_.height -eq 1080 } | Select-Object -First 1
        if (-not $mode) { $mode = $ready.displayModes | Select-Object -First 1 }
        if (-not $mode) { throw 'No supported virtual modes advertised.' }
        $process.StandardInput.WriteLine((@{seq=1;session=$true;displayPolicy=0;width=$mode.width;height=$mode.height;scale=1} | ConvertTo-Json -Compress))
        $resized = Read-Reply $process
        if ($resized.seq -ne 1 -or $resized.width -ne $mode.width -or $resized.height -ne $mode.height) { throw 'Resize acknowledgement mismatch.' }
        if ($scenario -eq 'forced-exit') {
            $guardInfo = Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'deskport-display-recovery.exe' -and $_.CommandLine -match ('lease ' + $process.Id + ' ') } | Select-Object -First 1
            if (-not $guardInfo) { throw 'Recovery guardian missing before crash test.' }
            $guard = Get-Process -Id $guardInfo.ProcessId
            $null = $guard.Handle
            $process.Kill()
            $guardExited = $guard.WaitForExit(20000)
            @{exited=$guardExited; exitCode=$(if ($guardExited) {$guard.ExitCode} else {$null})} | ConvertTo-Json | Set-Content (Join-Path $state 'guardian.json')
            $guard.Dispose()
        } else {
            $process.StandardInput.WriteLine((@{seq=2;session=$false;displayPolicy=0;width=$mode.width;height=$mode.height;scale=1} | ConvertTo-Json -Compress))
            $released = Read-Reply $process
            if ($released.seq -ne 2) { throw 'Release acknowledgement mismatch.' }
            $process.StandardInput.Close()
        }
        if (-not $process.WaitForExit(25000)) { throw 'Display cleanup timed out.' }
        if ($scenario -ne 'forced-exit' -and $process.ExitCode -ne 0) { throw "Helper failed: $($process.ExitCode)" }
        $restored = $false
        for ($attempt = 0; $attempt -lt 60; $attempt++) {
            $lines = & $recovery inspect
            if ($LASTEXITCODE -eq 0 -and ($lines -join "`n") -eq $baseline) { $restored = $true; break }
            Start-Sleep -Milliseconds 250
        }
        if (-not $restored) { throw 'Physical layout or disabled VDD baseline was not restored.' }
        $results += @{round=$round;scenario=$scenario;width=$resized.width;height=$resized.height;restored=$true}
        $results | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'results.json')
    } finally {
        if (-not $process.HasExited) {
            $process.StandardInput.Close()
            if (-not $process.WaitForExit(20000)) { $process.Kill(); $process.WaitForExit() }
        }
        if ($stderr -and $stderr.Wait(20000)) { $stderr.Result | Set-Content (Join-Path $state ($scenario + '.log')) }
        $process.Dispose()
    }
}
}
$results | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'results.json')
$results | ConvertTo-Json
