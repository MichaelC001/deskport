param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path $Executable).Path
$OutputDirectory = (New-Item -ItemType Directory -Force -Path $OutputDirectory).FullName

Add-Type -ReferencedAssemblies System.Drawing @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;
public static class DeskPortTitlebarProbe {
  [DllImport("kernel32.dll")] public static extern uint WTSGetActiveConsoleSessionId();
  [DllImport("kernel32.dll")] public static extern uint ProcessIdToSessionId(uint pid, out uint session);
  [DllImport("user32.dll")] public static extern IntPtr GetThreadDesktop(uint thread);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr hwnd, uint attribute, out int value, uint size);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public static bool CaptureTop(IntPtr hwnd, string path) {
    RECT r; if (!GetWindowRect(hwnd, out r)) return false;
    int width = r.Right-r.Left, height = Math.Min(r.Bottom-r.Top, 300);
    if (width <= 0 || height <= 0) return false;
    using (var bitmap = new Bitmap(width, height, PixelFormat.Format32bppArgb)) {
      using (var graphics = Graphics.FromImage(bitmap)) {
        graphics.CopyFromScreen(r.Left, r.Top, 0, 0, new Size(width, height), CopyPixelOperation.SourceCopy);
      }
      bitmap.Save(path, ImageFormat.Png);
    }
    return true;
  }
}
'@

# Session 0 and SSH sessions do not have the interactive desktop needed for a
# native title-bar screenshot. Do not launch the candidate in those sessions.
[uint32]$session = 0
[void][DeskPortTitlebarProbe]::ProcessIdToSessionId([uint32]$PID, [ref]$session)
$active = [DeskPortTitlebarProbe]::WTSGetActiveConsoleSessionId()
if ($active -eq [uint32]::MaxValue -or $session -eq 0 -or $session -ne $active -or
    [DeskPortTitlebarProbe]::GetThreadDesktop([DeskPortTitlebarProbe]::GetCurrentThreadId()) -eq [IntPtr]::Zero) {
    throw 'Interactive desktop session required; refusing SSH/session-0 execution.'
}
[void][DeskPortTitlebarProbe]::SetProcessDpiAwarenessContext([IntPtr](-4))

$temporary = Join-Path ([IO.Path]::GetTempPath()) ('deskport-titlebar-' + [guid]::NewGuid().ToString())
New-Item -ItemType Directory -Force $temporary | Out-Null
$results = @()
try {
    foreach ($mode in @(@{Name='light'; Value=1; Expected=0}, @{Name='dark'; Value=2; Expected=1})) {
        $profile = Join-Path $temporary $mode.Name
        New-Item -ItemType Directory -Force (Join-Path $profile 'DeskPort') | Out-Null
        # QSettings portable profile; no host identity, credentials, or real
        # user settings are read because the process starts in this directory.
        Set-Content -LiteralPath (Join-Path $profile 'portable.dat') -Value '' -NoNewline
        Set-Content -LiteralPath (Join-Path $profile 'DeskPort/DeskPort.ini') -Value "[ui]`nuiTheme=$($mode.Value)`n" -NoNewline
        $process = $null
        try {
            $start = New-Object System.Diagnostics.ProcessStartInfo
            $start.FileName = $Executable
            $start.Arguments = '--no-host-autostart'
            $start.WorkingDirectory = $profile
            $start.UseShellExecute = $false
            $start.CreateNoWindow = $false
            $process = [Diagnostics.Process]::Start($start)
            $deadline = [DateTime]::UtcNow.AddSeconds(20)
            $handle = [IntPtr]::Zero
            while ([DateTime]::UtcNow -lt $deadline -and $handle -eq [IntPtr]::Zero) {
                $process.Refresh()
                if ($process.HasExited) { throw "DeskPort exited before $($mode.Name) window appeared (code $($process.ExitCode))." }
                $handle = $process.MainWindowHandle
                if ($handle -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 100 }
            }
            if ($handle -eq [IntPtr]::Zero) { throw "Timed out waiting for $($mode.Name) window." }
            Start-Sleep -Milliseconds 500
            $actual = 0
            $hr = [DeskPortTitlebarProbe]::DwmGetWindowAttribute($handle, 20, [ref]$actual, 4)
            if ($hr -ne 0) { throw "DwmGetWindowAttribute(20) failed for $($mode.Name): HRESULT 0x$('{0:X8}' -f ([uint32]$hr))." }
            $png = Join-Path $OutputDirectory "titlebar-$($mode.Name).png"
            if (-not [DeskPortTitlebarProbe]::CaptureTop($handle, $png)) { throw "Could not capture $($mode.Name) title bar." }
            $title = New-Object Text.StringBuilder 256
            [void][DeskPortTitlebarProbe]::GetWindowText($handle, $title, 256)
            $results += [pscustomobject]@{ mode=$mode.Name; expected=[int]$mode.Expected; actual=$actual; hwnd=('0x{0:X}' -f $handle.ToInt64()); title=$title.ToString(); png=$png; hresult=$hr; pass=($actual -eq $mode.Expected) }
            if ($actual -ne $mode.Expected) { throw "Expected $($mode.Name) DWMWA_USE_IMMERSIVE_DARK_MODE=$($mode.Expected), got $actual." }
        } finally {
            if ($process -and -not $process.HasExited) { $process.CloseMainWindow() | Out-Null; if (-not $process.WaitForExit(3000)) { $process.Kill(); $process.WaitForExit() } }
            if ($process) { $process.Dispose() }
        }
    }
    $json = Join-Path $OutputDirectory 'titlebar-results.json'
    $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $json
    Write-Output ($results | ConvertTo-Json -Depth 4)
} finally {
    if (Test-Path $temporary) { Remove-Item -Recurse -Force $temporary }
}
