param(
 [Parameter(Mandatory=$true)][string]$Executable,
 [Parameter(Mandatory=$true)][string]$Probe,
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [ValidateRange(1,10)][int]$Rounds=2,
 [switch]$BrokerCrash
)
# Explicit native acceptance only, in a reserved interactive console session.
# Baseline fixture writes are limited to the exact owned virtual source and are
# restored in finally. The observer window separately verifies effective UI DPI.
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
$Executable=(Resolve-Path $Executable).Path;$Probe=(Resolve-Path $Probe).Path
$OutputDirectory=(New-Item -ItemType Directory -Force $OutputDirectory).FullName
if((Get-Process -Id $PID).SessionId -eq 0){throw 'Interactive console required'}
if(Get-Process deskport-display -ErrorAction SilentlyContinue){throw 'Stop sharing before DPI acceptance'}
$recovery=Join-Path (Split-Path $Executable) 'deskport-display-recovery.exe'
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

function Observe([int]$SetPercentage=0) {
 $n=New-Object Diagnostics.Process;$n.StartInfo.FileName=$Probe;$n.StartInfo.UseShellExecute=$false;$n.StartInfo.CreateNoWindow=$true;$n.StartInfo.RedirectStandardOutput=$true;$n.StartInfo.RedirectStandardError=$true
 if($SetPercentage){$n.StartInfo.Arguments="--test-set-owned $SetPercentage"}
 $null=$n.Start();$o=$n.StandardOutput.ReadToEndAsync();$e=$n.StandardError.ReadToEndAsync()
 if(!$n.WaitForExit(15000)){$n.Kill();throw 'DPI observer timed out'}
 if($n.ExitCode){throw "DPI observer failed: $($n.ExitCode) $($e.Result)"};$raw=$o.Result;$n.Dispose(); $rows=$raw|ConvertFrom-Json;return $rows
}
function Physical($Rows) {
 return (@($Rows|Where-Object{!$_.owned}|Sort-Object adapterHigh,adapterLow,target|Select-Object adapterHigh,adapterLow,target,windowDpi,width,height)|ConvertTo-Json -Compress)
}
function Reply($P) {
 $r=$P.StandardOutput.ReadLineAsync();if(!$r.Wait(25000)){throw 'Display reply timed out'}
 if(!$r.Result){throw 'Display helper exited before reply'};$v=$r.Result|ConvertFrom-Json;if($v.error){throw $v.error};return $v
}
function Start-Helper {
 $p=New-Object Diagnostics.Process;$p.StartInfo.FileName=$Executable;$p.StartInfo.Arguments='1920 1080';$p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true;$p.StartInfo.RedirectStandardInput=$true;$p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
 $state=Join-Path $OutputDirectory ([guid]::NewGuid().ToString());New-Item -ItemType Directory $state|Out-Null;$p.StartInfo.EnvironmentVariables['DESKPORT_DISPLAY_STATE_DIR']=$state
 $null=$p.Start();$log=$p.StandardError.ReadToEndAsync();$ready=Reply $p;if(!$ready.virtual){throw 'Owned VDD required'}
 $guards=@(Get-Process deskport-display-recovery -ErrorAction Stop|Where-Object{$_.SessionId -eq (Get-Process -Id $PID).SessionId});if($guards.Count -ne 1){throw 'Ambiguous recovery guardian'}
 $handle=[DeskPortNativeGuardian]::Open($guards[0].Id,$recovery)
 return @{p=$p;log=$log;state=$state;guardian=$handle}
}
function Finish-Helper($H,[switch]$Killed) {
 $p=$H.p;if(!$p.HasExited){$p.StandardInput.Close();if(!$p.WaitForExit(35000)){$p.Kill();throw 'Helper cleanup timed out'}}
 if(![DeskPortNativeGuardian]::Wait($H.guardian,25000)){throw 'DPI guardian cleanup timed out'}
 $code=[DeskPortNativeGuardian]::ExitCode($H.guardian);[DeskPortNativeGuardian]::CloseHandle($H.guardian)|Out-Null;$H.guardian=[IntPtr]::Zero
 if($H.log.Wait(10000)){$H.log.Result|Set-Content (Join-Path $H.state 'private-helper.log')};$p.Dispose()
 if($code){throw "DPI guardian recovery failed: $code"}
 $rows=Observe;if(@($rows|Where-Object{$_.owned}).Count -or (Physical $rows) -ne $script:physical){throw 'Physical layout/DPI was not restored'}
}
function Request($H,[int]$Sequence,[int]$Width,[int]$Height,[int]$Scale,[int]$Policy=2) {
 $H.p.StandardInput.WriteLine((@{seq=$Sequence;session=$true;width=$Width;height=$Height;scale=$Scale;displayPolicy=$Policy}|ConvertTo-Json -Compress));return Reply $H.p
}
function Assert-Dpi($Reply,[int]$Expected,[switch]$PhysicalPresent) {
 if($Reply.scale -ne $Expected/100 -or $Reply.dpiPercent -ne $Expected){throw "Incorrect scale acknowledgement: $($Reply|ConvertTo-Json -Compress)"}
 $rows=Observe;$owned=@($rows|Where-Object{$_.owned});if($owned.Count -ne 1 -or $owned[0].percent -ne $Expected -or $owned[0].windowDpi -ne $Expected*96/100){throw "Actual window DPI differs from acknowledgement: $($rows|ConvertTo-Json -Compress)"}
 if($PhysicalPresent -and (Physical $rows) -ne $script:physical){throw 'Physical display DPI/layout changed'}
 return $owned[0]
}
$script:physical=Physical (Observe);$results=@();$disconnects=@();$h=$null;$original=0;$script:sequence=0
try {
 foreach($round in 1..$Rounds){foreach($policy in @(2,1)) {
  $h=Start-Helper;$initial=@(Observe|Where-Object{$_.owned})[0];if(!$original){$original=$initial.percent}
  $null=Observe 125 # Deliberately non-default, owned-output-only fixture baseline.
  $firstSource=$null;$caseIndex=0
  foreach($case in @(@(2560,1440,2,200),@(2560,1440,1,100),@(2560,1440,2,200),@(2136,1200,2,200),@(1600,2848,2,200),@(1920,1080,2,100))) {
   $reply=Request $h (++$script:sequence) $case[0] $case[1] $case[2] $policy
   $observed=Assert-Dpi $reply $case[3] -PhysicalPresent:($policy -eq 2)
   if($caseIndex -eq 0){$firstSource=$observed}
   elseif($caseIndex -in @(1,2)){if($observed.adapterLow -ne $firstSource.adapterLow -or $observed.adapterHigh -ne $firstSource.adapterHigh -or $observed.source -ne $firstSource.source){throw 'Same-pixel scale toggle recreated the source'}}
   if($case[3] -eq 100 -and $case[2] -eq 2 -and !$reply.dpiLimited){throw 'Missing native-scale limitation warning'}
   $caseIndex++
   $results+=@{round=$round;policy=$policy;width=$case[0];height=$case[1];requestedScale=$case[2];actualPercent=$observed.percent;windowDpi=$observed.windowDpi;physicalDpiUntouched=$true}
   $results|ConvertTo-Json -Depth 5|Set-Content (Join-Path $OutputDirectory 'results.json')
  }
  $null=Assert-Dpi (Request $h (++$script:sequence) 2560 1440 2 $policy) 200 -PhysicalPresent:($policy -eq 2)
  $h.p.StandardInput.WriteLine((@{seq=++$script:sequence;session=$false}|ConvertTo-Json -Compress));$null=Reply $h.p
  $restored=@(Observe|Where-Object{$_.owned})[0];if($restored.percent -ne 125 -or $restored.windowDpi -ne 120){throw 'Disconnect failed to restore the original 125% DPI'}
  $disconnects+=@{round=$round;policy=$policy;restored=$restored};$disconnects|ConvertTo-Json -Depth 5|Set-Content (Join-Path $OutputDirectory 'disconnect-restores.json')
  $null=Observe $original;Finish-Helper $h;$h=$null
 }}
 # Mirroring inherits the physical desktop; a 2x request must not change
 # either source's saved DPI. Use its existing size to avoid mode differences.
 $h=Start-Helper;$mirrorBefore=@(Observe|Where-Object{$_.owned})[0]
 $mirror=Request $h (++$script:sequence) 1920 1080 2 0
 if($mirror.scale -ne 1){throw 'Mirror must retain the existing 1x downgrade'}
 $rows=Observe;$mirrorAfter=@($rows|Where-Object{$_.owned})[0]
 # A clone has no independent GDI desktop and Windows can reject its DPI query.
 # Verify the physical desktop while cloned, then the saved virtual DPI after detach.
 if(($mirrorAfter.queryError -eq 0 -and $mirrorAfter.percent -ne $mirrorBefore.percent) -or (Physical $rows) -ne $script:physical){throw 'Mirror changed saved virtual or physical DPI'}
 @{before=$mirrorBefore;during=$mirrorAfter}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $OutputDirectory 'mirror-observation.json')
 $h.p.StandardInput.WriteLine((@{seq=++$script:sequence;session=$false}|ConvertTo-Json -Compress));$null=Reply $h.p
 $mirrorRestored=@(Observe|Where-Object{$_.owned})[0]
 if($mirrorRestored.percent -ne $mirrorBefore.percent -or $mirrorRestored.windowDpi -ne $mirrorBefore.windowDpi){throw 'Mirror changed saved virtual DPI after detach'}
 Finish-Helper $h;$h=$null
 @{passed=$true;systemDpiWritesRequired=$false;physicalDpiUntouched=$true}|ConvertTo-Json|Set-Content (Join-Path $OutputDirectory 'mirror.json')
 foreach($failure in @('helper','broker')) {
  if($failure -eq 'broker' -and !$BrokerCrash){continue}
  $h=Start-Helper;$baseline=Observe 125;$reply=Request $h (++$script:sequence) 2560 1440 2 2;$scaled=Assert-Dpi $reply 200 -PhysicalPresent
  if($failure -eq 'helper'){$h.p.Kill();$h.p.WaitForExit()}
  else {
   'ready'|Set-Content (Join-Path $OutputDirectory 'broker-armed')
   $deadline=(Get-Date).AddSeconds(60)
   do{Start-Sleep -Milliseconds 200}while(![IO.File]::Exists((Join-Path $OutputDirectory 'broker-triggered')) -and (Get-Date) -lt $deadline)
   if((Get-Date) -ge $deadline){throw 'Admin coordinator did not trigger broker crash'}
   if(![DeskPortNativeGuardian]::Wait($h.guardian,25000)){throw 'Broker death did not restore independently'}
   if($h.p.HasExited){throw 'Helper exited before independent recovery observation'}
   $rows=Observe;if(@($rows|Where-Object{$_.owned}).Count -or (Physical $rows) -ne $script:physical){throw 'Broker death did not restore physical layout/DPI'}
   $h.p.Kill();$h.p.WaitForExit()
  }
  Finish-Helper $h -Killed;$h=$null
  if($failure -eq 'broker') {
   # SCM retains its failure count across upgrades: the configured second and
   # subsequent restart delay is 30 seconds, not the first-crash 5 seconds.
   $deadline=(Get-Date).AddSeconds(65)
   do{Start-Sleep -Milliseconds 250;$running=(Get-Service DeskPortDisplayService).Status -eq 'Running'}while(!$running -and (Get-Date) -lt $deadline)
   if(!$running){throw 'SCM did not restart the automatic display service'}
   Start-Sleep -Milliseconds 500
  }else{Start-Sleep -Seconds 6}
  $h=Start-Helper;$restored=@(Observe|Where-Object{$_.owned})[0];@{baseline=$baseline;scaled=$scaled;reenabled=$restored}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $OutputDirectory "$failure-observation.json");if($restored.percent -ne 125 -or $restored.windowDpi -ne 120){throw "$failure crash failed to restore original virtual DPI"}
  $null=Observe $original;Finish-Helper $h;$h=$null
  @{failure=$failure;originalPercentRestored=125;physicalDpiUntouched=$true}|ConvertTo-Json|Set-Content (Join-Path $OutputDirectory "$failure-recovery.json")
 }
 @{passed=$true;scaleCases=$results.Count;rounds=$Rounds;originalOwnedPercentage=$original;baselineFixturePercentage=125;physicalRestored=$true}|ConvertTo-Json|Set-Content (Join-Path $OutputDirectory 'summary.json')
} finally {
 if($h){try{if(!$h.p.HasExited){$h.p.StandardInput.WriteLine((@{seq=++$script:sequence;session=$false}|ConvertTo-Json -Compress));$null=Reply $h.p;if($original){$null=Observe $original}};Finish-Helper $h}catch{if($h.p -and !$h.p.HasExited){$h.p.Kill()};throw}}
}
