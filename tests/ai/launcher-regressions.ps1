param([string]$Stage, [string]$WorkRoot, [string]$Shortcut = '')
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$appPath = [IO.Path]::GetFullPath((Join-Path $Stage 'gusek.exe'))
$profile = if ($Shortcut) { Join-Path $WorkRoot 'shortcut-profile' } else { Join-Path $Stage 'work\assistant' }
$config = if ($Shortcut) { Join-Path $WorkRoot 'shortcut-config' } else { Join-Path $Stage 'work\config' }
New-Item -ItemType Directory -Force -Path $profile,$config | Out-Null
Set-Content -LiteralPath (Join-Path $profile 'GusekAI.ini') -Value 'enabled=no' -Encoding ASCII

if (-not ('LauncherWindow' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class LauncherWindow {
    delegate bool EnumCallback(IntPtr window, IntPtr unused);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumCallback callback, IntPtr unused);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder name, int size);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
    [DllImport("kernel32.dll")] public static extern bool GetExitCodeProcess(IntPtr process, out uint code);
    public static IntPtr Find(uint pid) {
        IntPtr result = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr unused) {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner == pid) {
                StringBuilder name = new StringBuilder(128);
                GetClassName(window, name, 128);
                if (name.ToString() == "SciTEWindow") { result = window; return false; }
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
}
'@
}
$savedEnv = @{}
foreach ($name in @('GUSEK_AI_DATA','SciTE_HOME','SciTE_USERHOME')) {
    $savedEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$process = $null
try {
    $env:GUSEK_AI_DATA = $profile
    $env:SciTE_HOME = $Stage
    $env:SciTE_USERHOME = $config
    $arguments = @('-check.if.already.open=0','-save.session=0','-save.position=0','-save.recent=0')
    if ($Shortcut) {
        Start-Process -FilePath $Shortcut -ArgumentList $arguments -WindowStyle Hidden | Out-Null
    } else {
        & (Join-Path $Stage 'Start-Gusek.cmd') @arguments
        if ($LASTEXITCODE -ne 0) { throw 'Portable launcher failed.' }
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $window = [IntPtr]::Zero
    while ([DateTime]::UtcNow -lt $deadline -and $window -eq [IntPtr]::Zero) {
        foreach ($candidate in @(Get-Process -Name gusek -ErrorAction SilentlyContinue)) {
            try {
                if ($candidate.Path -eq $appPath) {
                    $process = $candidate
                    $window = [LauncherWindow]::Find([uint32]$candidate.Id)
                    if ($window -ne [IntPtr]::Zero) { break }
                }
            } catch { }
        }
        if ($window -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 50 }
    }
    if ($window -eq [IntPtr]::Zero) { throw 'Packaged launcher did not open its own application window.' }
    Write-Host '[PASS] Packaged launcher opens the actual application window.'
    $launchedHandle = $process.Handle
    [LauncherWindow]::PostMessage($window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    $exited = $process.WaitForExit(5000)
    $launchExitCode = [uint32]259
    if (-not $exited -or -not [LauncherWindow]::GetExitCodeProcess($launchedHandle, [ref]$launchExitCode) -or $launchExitCode -ne 0) {
        throw "Launched application did not exit cleanly (finished=$exited, code=$launchExitCode)."
    }
    Write-Host '[PASS] Packaged launcher uses an isolated profile and closes cleanly.'
} finally {
    # Only the exact executable from this unique test installation is owned here.
    if ($process -and -not $process.HasExited -and $process.Path -eq $appPath) {
        Stop-Process -Id $process.Id -Force
    }
    foreach ($name in $savedEnv.Keys) { [Environment]::SetEnvironmentVariable($name,$savedEnv[$name],'Process') }
}
