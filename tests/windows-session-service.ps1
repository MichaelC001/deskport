param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
# Development-only, read-only desktop-access probe. Run elevated over SSH as
# the same user who owns the active console. Removes its service/task in finally.
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path $Executable).Path
$expected = Join-Path $env:ProgramFiles 'DeskPort\host\deskport-session-probe.exe'
if ($Executable -ine $expected) { throw 'Place the probe in the administrator-owned DeskPort host directory.' }
$serviceName = 'DeskPortSessionProbe'
if (Get-Service $serviceName -ErrorAction SilentlyContinue) { throw 'An existing probe service was not replaced.' }
if (Test-Path $OutputDirectory) { throw 'Use a new output directory so earlier evidence is preserved.' }
$OutputDirectory = (New-Item -ItemType Directory $OutputDirectory).FullName
$taskName = 'DeskPortSessionProbe-' + [guid]::NewGuid().ToString('N')
$taskCreated = $false
$serviceCreated = $false
$silentClient = $null
$results = @()
function Record($name, $passed, $detail) {
    $script:results += @{name=$name;passed=[bool]$passed;detail=$detail}
    $script:results | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutputDirectory 'results.json')
    if (-not $passed) { throw "$name failed: $detail" }
}
function Literal($text) { return "'" + $text.Replace("'", "''") + "'" }
try {
    New-Service -Name $serviceName -BinaryPathName ('"' + $Executable + '" --service') -StartupType Manual | Out-Null
    $serviceCreated = $true
    Start-Service $serviceName
    $reply = & $Executable --probe
    Record 'session-zero-rejected' ($LASTEXITCODE -eq 5) ($reply -join ' ')

    $interactiveResults = Join-Path $OutputDirectory 'interactive.json'
    $scriptPath = Join-Path $OutputDirectory 'interactive.ps1'
    $script = @'
$ErrorActionPreference='Stop'
$results=@()
foreach($case in @(@('--probe',0),@('--invalid',13),@('--anonymous',5),@('--worker',5),@('--unknown',87),@('--low',5))) {
    $output=& __EXECUTABLE__ $case[0]
    $results+=@{name=$case[0];expected=$case[1];actual=$LASTEXITCODE;output=($output -join ' ')}
}
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class ForeignPipeProbe {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFile(string path,uint access,uint share,IntPtr security,uint create,uint flags,IntPtr template);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool WriteFile(IntPtr pipe,byte[] data,uint count,out uint bytes,IntPtr ov);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool ReadFile(IntPtr pipe,byte[] data,uint count,out uint bytes,IntPtr ov);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
 public static int Run() {
  IntPtr pipe=CreateFile(@"\\.\pipe\DeskPort.SessionProbe.v1",0x00100003,0,IntPtr.Zero,3,0x00120000,IntPtr.Zero);
  if(pipe.ToInt64()==-1)return -Marshal.GetLastWin32Error();
  try {
   byte[] data=new byte[16];Buffer.BlockCopy(new uint[]{0x44505350,1,1,0},0,data,0,16);uint bytes;
   if(!WriteFile(pipe,data,16,out bytes,IntPtr.Zero)||!ReadFile(pipe,data,16,out bytes,IntPtr.Zero)||bytes!=16)return -Marshal.GetLastWin32Error();
   int code=BitConverter.ToInt32(data,8);
   WriteFile(pipe,data,16,out bytes,IntPtr.Zero);
   return code;
  } finally {CloseHandle(pipe);}
 }
}
"@
$results+=@{name='foreign-image';expected=5;actual=[ForeignPipeProbe]::Run();output='Authenticated console user, different executable'}
$temporary=__RESULT__ + '.tmp'
$results | ConvertTo-Json | Set-Content $temporary
Move-Item -LiteralPath $temporary -Destination __RESULT__
'@
    $script.Replace('__EXECUTABLE__', (Literal $Executable)).Replace('__RESULT__', (Literal $interactiveResults)) | Set-Content -Encoding UTF8 $scriptPath
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument ('-NoProfile -ExecutionPolicy Bypass -File "' + $scriptPath + '"')
    $user = [Security.Principal.WindowsIdentity]::GetCurrent().Name
    $principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited
    Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal | Out-Null
    $taskCreated = $true
    Start-ScheduledTask $taskName
    $deadline = (Get-Date).AddSeconds(45)
    do {
        Start-Sleep -Milliseconds 250
        $finished = Test-Path $interactiveResults
        if ($finished) { $cases = Get-Content $interactiveResults -Raw | ConvertFrom-Json; $finished = @($cases).Count -eq 7 }
    } until ($finished -or (Get-Date) -gt $deadline)
    Record 'interactive-completed' $finished 'Bounded standard-user task'
    foreach ($case in $cases) {
        $passed=$case.actual -eq $case.expected
        if($case.name -eq '--low') {$passed=$passed -and $case.output -match 'integrity=low'}
        Record ('interactive' + $case.name) $passed $case
    }

    $silentClient = New-Object IO.Pipes.NamedPipeClientStream('.', 'DeskPort.SessionProbe.v1', [IO.Pipes.PipeDirection]::InOut)
    $silentClient.Connect(5000)
    $buffer=New-Object byte[] 16
    $pending=$silentClient.ReadAsync($buffer,0,16)
    $timedOut=$pending.Wait(8000) -and $pending.Result -eq 16 -and [BitConverter]::ToUInt32($buffer,8) -eq 1460
    Record 'silent-request-deadline' $timedOut 'Expected ERROR_TIMEOUT without a request'
    $silentClient.Write($buffer,0,16)
    $silentClient.Dispose(); $silentClient=$null

    # A connected client sending no request must not prevent service shutdown.
    $silentClient = New-Object IO.Pipes.NamedPipeClientStream('.', 'DeskPort.SessionProbe.v1', [IO.Pipes.PipeDirection]::InOut)
    $silentClient.Connect(5000)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Stop-Service $serviceName
    $watch.Stop()
    Record 'stop-with-silent-client' ($watch.ElapsedMilliseconds -lt 5000) $watch.ElapsedMilliseconds
    $silentClient.Dispose(); $silentClient = $null
    $fakeServer = New-Object IO.Pipes.NamedPipeServerStream('DeskPort.SessionProbe.v1')
    try {
        $rejected=$false
        try { Start-Service $serviceName } catch { $rejected=$true }
        Record 'preexisting-pipe-rejected' $rejected 'First-instance guard'
    } finally { $fakeServer.Dispose() }
    Start-Service $serviceName
    $reply = & $Executable --probe
    Record 'restart-retains-session-denial' ($LASTEXITCODE -eq 5) ($reply -join ' ')
} finally {
    if ($silentClient) { $silentClient.Dispose() }
    if ($taskCreated) {
        Stop-ScheduledTask $taskName -ErrorAction SilentlyContinue
        Unregister-ScheduledTask $taskName -Confirm:$false
    }
    if ($serviceCreated) {
        Stop-Service $serviceName -ErrorAction SilentlyContinue
        & sc.exe delete $serviceName | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Probe service cleanup failed.' }
    }
}
$results | ConvertTo-Json -Depth 4
