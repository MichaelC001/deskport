param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateRange(1, 100)][int]$Rounds = 1,
    [switch]$PortraitSwitch,
    [ValidateRange(0, 2)][int]$Policy = 2,
    [switch]$CustomSize,
    [switch]$DynamicSwitch,
    [switch]$ReusePolicies
)

# Explicit native acceptance: temporarily enables only DeskPort's owned VDD.
# Run in the interactive console session, not directly in SSH/session 0.
# The installed display service permits a medium-integrity test runner.
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path $Executable).Path
$OutputDirectory = (New-Item -ItemType Directory -Force $OutputDirectory).FullName
$recovery = Join-Path (Split-Path $Executable) 'deskport-display-recovery.exe'
if ((Get-Process -Id $PID).SessionId -eq 0) { throw 'Interactive session required.' }
if (Get-Process deskport-display -ErrorAction SilentlyContinue) { throw 'Stop existing sharing before this test.' }
if (-not ('DeskPortNativeGuardian' -as [type])) {
Add-Type @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
public static class DeskPortNativeGuardian {
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,int pid);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool QueryFullProcessImageName(IntPtr process,uint flags,StringBuilder name,ref int size);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr process);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr process,uint timeout);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr process,out uint code);
    public static IntPtr Open(int pid,string expected) {
        // Match the broker's exact logon grant: query/synchronize, never ALL_ACCESS.
        var handle=OpenProcess(0x00101000,false,pid);
        if(handle==IntPtr.Zero)throw new Win32Exception();
        var path=new StringBuilder(32768);int size=path.Capacity;
        if(!QueryFullProcessImageName(handle,0,path,ref size)||!String.Equals(path.ToString(),expected,StringComparison.OrdinalIgnoreCase)) {
            CloseHandle(handle);throw new InvalidOperationException("Unexpected guardian executable");
        }
        return handle;
    }
    public static bool Wait(IntPtr handle,uint timeout){return WaitForSingleObject(handle,timeout)==0;}
    public static uint ExitCode(IntPtr handle){uint code;if(!GetExitCodeProcess(handle,out code))throw new Win32Exception();return code;}
}
'@
}

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
'[]' | Set-Content (Join-Path $OutputDirectory 'results.json')
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
        if (-not $mode) { $mode = @{width=1920;height=1080} }
        if ($CustomSize) { $mode = @{width=1300;height=748} }
        $watch=[Diagnostics.Stopwatch]::StartNew()
        $process.StandardInput.WriteLine((@{seq=1;session=$true;displayPolicy=$Policy;width=$mode.width;height=$mode.height;scale=1} | ConvertTo-Json -Compress))
        $resized = Read-Reply $process
        $resizeMilliseconds=$watch.ElapsedMilliseconds
        if ($resized.seq -ne 1 -or $resized.width -ne $mode.width -or $resized.height -ne $mode.height) { throw 'Resize acknowledgement mismatch.' }
        $sequence = 1
        if ($PortraitSwitch) {
            foreach ($size in @(@{width=1080;height=1920}, @{width=1920;height=1080})) {
                if ($ready.displayModes.Count -gt 0 -and -not ($ready.displayModes | Where-Object { $_.width -eq $size.width -and $_.height -eq $size.height })) { throw 'Required orientation mode missing.' }
                $sequence++
                $process.StandardInput.WriteLine((@{seq=$sequence;session=$true;displayPolicy=$Policy;width=$size.width;height=$size.height;scale=1} | ConvertTo-Json -Compress))
                $resized = Read-Reply $process
                if ($resized.seq -ne $sequence -or $resized.width -ne $size.width -or $resized.height -ne $size.height) { throw 'Orientation acknowledgement mismatch.' }
            }
        }
        if ($DynamicSwitch) {
            foreach ($size in @(@{width=1308;height=756}, @{width=1312;height=760})) {
                $sequence++
                $process.StandardInput.WriteLine((@{seq=$sequence;session=$true;displayPolicy=$Policy;width=$size.width;height=$size.height;scale=1} | ConvertTo-Json -Compress))
                $resized = Read-Reply $process
                if ($resized.seq -ne $sequence -or $resized.width -ne $size.width -or $resized.height -ne $size.height) { throw 'Dynamic resize acknowledgement mismatch.' }
            }
        }
        if ($ReusePolicies) {
            foreach ($nextPolicy in @(0,1,2)) {
                $sequence++
                $process.StandardInput.WriteLine((@{seq=$sequence;session=$false;displayPolicy=$Policy;width=1920;height=1080;scale=1} | ConvertTo-Json -Compress))
                $null = Read-Reply $process
                $sequence++
                $process.StandardInput.WriteLine((@{seq=$sequence;session=$true;displayPolicy=$nextPolicy;width=1920;height=1080;scale=1} | ConvertTo-Json -Compress))
                $resized = Read-Reply $process
                if ($resized.seq -ne $sequence -or $resized.width -ne 1920 -or $resized.height -ne 1080) { throw 'Reused lease acknowledgement mismatch.' }
            }
        }
        if ($scenario -eq 'forced-exit') {
            # WMI hides a SYSTEM command line from a medium user. The exclusive
            # test must have one guardian; authenticate its image with limited
            # query access and retain that handle across the helper's death.
            $guards = @(Get-Process deskport-display-recovery -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $process.SessionId })
            if ($guards.Count -ne 1) { throw 'Expected one recovery guardian before crash test.' }
            $guard = [DeskPortNativeGuardian]::Open($guards[0].Id, $recovery)
            try {
                $process.Kill()
                $guardExited = [DeskPortNativeGuardian]::Wait($guard, 20000)
                @{exited=$guardExited; exitCode=$(if ($guardExited) {[DeskPortNativeGuardian]::ExitCode($guard)} else {$null})} | ConvertTo-Json | Set-Content (Join-Path $state 'guardian.json')
                if (-not $guardExited) { throw 'Guardian did not restore after helper death.' }
            } finally { [void][DeskPortNativeGuardian]::CloseHandle($guard) }
        } else {
            $sequence++
            $process.StandardInput.WriteLine((@{seq=$sequence;session=$false;displayPolicy=$Policy;width=$mode.width;height=$mode.height;scale=1} | ConvertTo-Json -Compress))
            $released = Read-Reply $process
            if ($released.seq -ne $sequence) { throw 'Release acknowledgement mismatch.' }
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
        $results += @{round=$round;scenario=$scenario;width=$resized.width;height=$resized.height;portraitSwitch=[bool]$PortraitSwitch;restored=$true;resizeMilliseconds=$resizeMilliseconds;policy=$Policy;dynamicSwitch=[bool]$DynamicSwitch;reusePolicies=[bool]$ReusePolicies}
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
