param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$Version
)
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path $Executable).Path
$metadata = (Get-Item $Executable).VersionInfo
foreach ($actual in @($metadata.FileVersion, $metadata.ProductVersion)) {
    if ($actual -ne "$Version.0") { throw "PE version mismatch: $actual != $Version.0" }
}
$temporary = Join-Path ([IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString())
New-Item -ItemType Directory $temporary | Out-Null
try {
    foreach ($argument in @('--version', '-v', '--help', '-h')) {
        $process = New-Object System.Diagnostics.Process
        $process.StartInfo.FileName = $Executable
        $process.StartInfo.Arguments = $argument
        $process.StartInfo.WorkingDirectory = $temporary
        $process.StartInfo.UseShellExecute = $false
        $process.StartInfo.RedirectStandardOutput = $true
        $process.StartInfo.RedirectStandardError = $true
        $process.StartInfo.CreateNoWindow = $true
        $null = $process.Start()
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(10000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "$argument hung without an interactive desktop"
        }
        if ($process.ExitCode -ne 0) {
            throw "$argument failed ($($process.ExitCode)): $($stderr.Result)"
        }
        $output = $stdout.Result
        $process.Dispose()
        if ($argument -in @('--version', '-v')) {
            if ($output.Trim() -ne "DeskPort $Version") { throw "Unexpected version: $output" }
        } elseif ($output -notmatch 'Available actions:' -or $output -notmatch 'DeskPort') {
            throw "Missing DeskPort command-line help: $output"
        }
        Write-Output "PASS: $argument exits and writes to redirected stdout"
    }
} finally {
    Remove-Item -Recurse -Force $temporary
}
