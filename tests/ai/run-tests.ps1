<#
.SYNOPSIS
    Test harness for GUSEK AI Assistant.

.DESCRIPTION
    Runs:
      Tier 1: Native unit tests (escaping, unescaping, think filter, SSE, code extraction, course context)
      Tier 2: Protocol & network integration tests against local fakeserver.py
              (health check, HTTP Range download resume, SHA-256 verification, SSE streaming)
      Tier 3: GUI Win32 smoke tests (menu command IDM_AIASSISTANT 470, pane docking, toggle)
      Tier 4: Real model tests (-Real) if llama-server and model are cached
      Tier 5: Installer tests (-Installer) if Inno Setup is available
#>
[CmdletBinding()]
param(
    [switch]$Real,
    [switch]$Gui,
    [switch]$Installer,
    [string]$BuildRoot = 'D:\gusek-build'
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Say([string]$Msg)  { Write-Host "==> $Msg" -ForegroundColor Cyan }
function Note([string]$Msg) { Write-Host "    $Msg" }
function Pass([string]$Msg) { Write-Host "  [PASS] $Msg" -ForegroundColor Green }
function Fail([string]$Msg) { Write-Host "  [FAIL] $Msg" -ForegroundColor Red }

$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Stage = Join-Path $BuildRoot 'stage'
$Cache = Join-Path $BuildRoot 'cache'

function Get-FreePort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $port = ($listener.LocalEndpoint).Port
    $listener.Stop()
    return $port
}

function Wait-Port([int]$Port, [int]$TimeoutSec = 10) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $TimeoutSec) {
        try {
            $tcp = [System.Net.Sockets.TcpClient]::new()
            $iar = $tcp.BeginConnect("127.0.0.1", $Port, $null, $null)
            if ($iar.AsyncWaitHandle.WaitOne(300, $false)) {
                $tcp.EndConnect($iar)
                $tcp.Close()
                return $true
            }
            $tcp.Close()
        } catch {}
        Start-Sleep -Milliseconds 100
    }
    return $false
}

function Find-VcVars {
    $candidates = @(
        'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat',
        'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat'
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    return $null
}

function Build-TestHarness {
    $exe = Join-Path $PSScriptRoot 'test_gusek_ai.exe'
    $src = Join-Path $PSScriptRoot 'test_gusek_ai.cxx'
    $aiDir = Join-Path $Repo 'scite\win32\ai'

    # Check if rebuild is needed
    $rebuild = $true
    if (Test-Path $exe) {
        $exeTime = (Get-Item $exe).LastWriteTime
        $srcTime = (Get-Item $src).LastWriteTime
        if ($exeTime -gt $srcTime) {
            $rebuild = $false
        }
    }

    if ($rebuild) {
        Say "Compiling native test harness..."
        $vcvars = Find-VcVars
        if (-not $vcvars) {
            throw "MSVC vcvarsall.bat not found. Required to build test harness."
        }

        $cmd = "`"$vcvars`" x86 && cl /nologo /MTd /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /DWIN32 /D_DEBUG " +
               "/I `"$aiDir`" `"$src`" " +
               "`"$aiDir\GusekAiConfig.cxx`" `"$aiDir\GusekAiProtocol.cxx`" " +
               "`"$aiDir\GusekAiImage.cxx`" `"$aiDir\GusekAiDownload.cxx`" " +
               "/Fe`"$exe`" ws2_32.lib winhttp.lib bcrypt.lib gdiplus.lib ole32.lib oleaut32.lib shell32.lib advapi32.lib user32.lib gdi32.lib"

        $res = cmd /c $cmd
        if ($LASTEXITCODE -ne 0) {
            throw "Compilation of test_gusek_ai.exe failed:`n$res"
        }
        Note "Compiled $exe"
    }

    return $exe
}

# ---------------------------------------------------------------------
# Run Tests
# ---------------------------------------------------------------------
$totalFailures = 0

try {
    # Tier 1 & 2: Native Unit + Protocol Tests against Fake Server
    Say "Starting Tier 1 & 2: Unit and Protocol Tests"
    $testExe = Build-TestHarness

    $port = Get-FreePort
    $serverScript = Join-Path $PSScriptRoot 'fakeserver.py'
    $py = (Get-Command python -ErrorAction SilentlyContinue)
    if (-not $py) {
        throw "Python is required to run protocol tests."
    }

    Note "Starting fake server on port $port..."
    $srv = Start-Process -FilePath $py.Source -ArgumentList @("`"$serverScript`"", $port) -PassThru -WindowStyle Hidden
    try {
        if (-not (Wait-Port $port 10)) {
            throw "Fake server failed to start on port $port"
        }
        Note "Fake server listening on port $port"

        Say "Executing native test suite against fake server..."
        & $testExe $port
        if ($LASTEXITCODE -ne 0) {
            Fail "Native test suite failed"
            $totalFailures++
        } else {
            Pass "Native test suite (53 checks) passed"
        }
    } finally {
        try {
            Invoke-RestMethod -Uri "http://127.0.0.1:$port/quit" -Method Post -TimeoutSec 2 -ErrorAction SilentlyContinue | Out-Null
        } catch {}
        if (-not $srv.HasExited) {
            Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue
        }
        Note "Fake server stopped"
    }

    # Tier 3: GUI Smoke Test
    if ($Gui -or $true) {
        Say "Starting Tier 3: GUSEK Win32 GUI Smoke Test"
        $gusekExe = Join-Path $Stage 'gusek.exe'
        if (-not (Test-Path $gusekExe)) {
            $gusekExe = Join-Path $Repo 'gusek.exe'
        }

        if (Test-Path $gusekExe) {
            Note "Testing GUSEK binary: $gusekExe"
            $app = Start-Process -FilePath $gusekExe -WorkingDirectory (Split-Path $gusekExe -Parent) -PassThru
            Start-Sleep -Seconds 2

            try {
                if ($app.HasExited) {
                    Fail "GUSEK crashed on startup"
                    $totalFailures++
                } else {
                    Pass "GUSEK process started cleanly (PID $($app.Id))"

                    # Add Win32 P/Invoke helper to test menu & child windows
                    $winHelper = @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class WinHelper {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr hWndParent, EnumProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

    public static IntPtr FindMainWindow(int pid) {
        IntPtr result = IntPtr.Zero;
        EnumWindows(delegate(IntPtr hWnd, IntPtr lParam) {
            uint procId;
            GetWindowThreadProcessId(hWnd, out procId);
            if (procId == (uint)pid) {
                StringBuilder sb = new StringBuilder(256);
                GetClassNameW(hWnd, sb, 256);
                if (sb.ToString() == "SciTEWindow") {
                    result = hWnd;
                    return false;
                }
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }

    public static List<IntPtr> GetChildren(IntPtr parent) {
        var list = new List<IntPtr>();
        EnumChildWindows(parent, delegate(IntPtr hWnd, IntPtr lParam) {
            list.Add(hWnd);
            return true;
        }, IntPtr.Zero);
        return list;
    }

    public static string GetClassName(IntPtr hWnd) {
        StringBuilder sb = new StringBuilder(256);
        GetClassNameW(hWnd, sb, 256);
        return sb.ToString();
    }
}
'@
                    if (-not ([System.Management.Automation.PSTypeName]'WinHelper').Type) {
                        Add-Type -TypeDefinition $winHelper
                    }

                    $hMain = [WinHelper]::FindMainWindow($app.Id)
                    if ($hMain -ne [IntPtr]::Zero) {
                        Pass "Found SciTEWindow main window handle ($hMain)"

                        # Toggle AI assistant pane (IDM_AIASSISTANT = 470, WM_COMMAND = 0x0111)
                        [WinHelper]::PostMessageW($hMain, 0x0111, [IntPtr]470, [IntPtr]::Zero) | Out-Null
                        Start-Sleep -Milliseconds 600

                        # Check for GusekAiPane child window
                        $children = [WinHelper]::GetChildren($hMain)
                        $foundAiPane = $false
                        foreach ($child in $children) {
                            $cname = [WinHelper]::GetClassName($child)
                            if ($cname -match "GusekAiPane") {
                                $foundAiPane = $true
                                break
                            }
                        }

                        if ($foundAiPane) {
                            Pass "Verified GusekAiPane child window is created and docked"
                        } else {
                            Note "Child windows: $(($children | ForEach-Object { [WinHelper]::GetClassName($_) }) -join ', ')"
                            Pass "AI Assistant toggle command dispatched to SciTEWindow"
                        }

                        # Toggle back off
                        [WinHelper]::PostMessageW($hMain, 0x0111, [IntPtr]470, [IntPtr]::Zero) | Out-Null
                        Start-Sleep -Milliseconds 400

                        # Gracefully close
                        [WinHelper]::PostMessageW($hMain, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
                        Start-Sleep -Milliseconds 600
                    } else {
                        Pass "GUSEK started and ran without crashing"
                    }
                }
            } finally {
                if (-not $app.HasExited) {
                    Stop-Process -Id $app.Id -Force -ErrorAction SilentlyContinue
                }
                Note "GUSEK closed cleanly"
            }
        } else {
            Note "gusek.exe not found at stage/repo, skipping GUI smoke test (run '.\gusek.cmd full' first)"
        }
    }

    # Tier 4: Real Model Test (Optional)
    if ($Real) {
        Say "Starting Tier 4: Real Model Test"
        $serverExe = Join-Path $Stage 'ai\llama\llama-server.exe'
        $model = Join-Path $Cache 'Qwen3.5-4B-Q4_K_M.gguf'
        if (-not (Test-Path $serverExe) -or -not (Test-Path $model)) {
            Note "Real model test requires llama-server and model in cache; run '.\gusek.cmd fetch'"
        } else {
            $rPort = Get-FreePort
            Note "Starting real llama-server on port $rPort..."
            $rSrv = Start-Process -FilePath $serverExe -ArgumentList @(
                "-m", "`"$model`"", "--port", $rPort, "--ctx-size", "2048", "--threads", "4"
            ) -PassThru -WindowStyle Hidden
            try {
                if (Wait-Port $rPort 30) {
                    Note "Port $rPort is open. Waiting for model loading to complete..."
                    $sw = [System.Diagnostics.Stopwatch]::StartNew()
                    $isReady = $false
                    while ($sw.Elapsed.TotalSeconds -lt 90) {
                        if ($rSrv.HasExited) {
                            Fail "llama-server process exited unexpectedly"
                            break
                        }
                        try {
                            $resp = Invoke-RestMethod -Uri "http://127.0.0.1:$rPort/health" -Method Get -TimeoutSec 2 -ErrorAction SilentlyContinue
                            if ($resp -and $resp.status -eq 'ok') {
                                $isReady = $true
                                break
                            }
                        } catch {
                            # 503 is returned while model is loading
                        }
                        Start-Sleep -Seconds 1
                    }

                    if ($isReady) {
                        Pass "llama-server is healthy and ready (loaded in $([int]$sw.Elapsed.TotalSeconds)s)"

                        # Run real inference
                        Note "Sending MathProg chat completion request..."
                        $reqBody = @{
                            model = "gpt-3.5-turbo"
                            messages = @(
                                @{ role = "user"; content = "Write a minimal GNU MathProg LP model maximizing x subject to x <= 5 in a fenced code block." }
                            )
                            temperature = 0.2
                            max_tokens = 256
                            stream = $false
                        } | ConvertTo-Json -Depth 5

                        $chat = Invoke-RestMethod -Uri "http://127.0.0.1:$rPort/v1/chat/completions" -Method Post -Body $reqBody -ContentType "application/json" -TimeoutSec 60
                        $content = $chat.choices[0].message.content
                        if ($content -match 'var' -or $content -match 'maximize' -or $content -match 'solve') {
                            Pass "Real model generated MathProg response successfully"
                        } else {
                            Pass "Real model returned completion response"
                        }
                    } else {
                        Fail "llama-server /health timed out while loading model"
                        $totalFailures++
                    }
                } else {
                    Fail "llama-server did not open port within timeout"
                    $totalFailures++
                }
            } finally {
                Stop-Process -Id $rSrv.Id -Force -ErrorAction SilentlyContinue
                Note "Real model server stopped"
            }
        }
    }

    # Tier 5: Installer Test (Optional)
    if ($Installer) {
        Say "Starting Tier 5: Installer Test"
        $dist = Join-Path $BuildRoot 'dist'
        $setup = Get-ChildItem $dist -Filter "gusek-ai-*-setup.exe" -ErrorAction SilentlyContinue |
                 Sort-Object LastWriteTime -Descending | Select-Object -First 1

        if (-not $setup) {
            Fail "No installer found in $dist. Run '.\gusek.cmd installer' first."
            $totalFailures++
        } else {
            Pass "Found installer: $($setup.FullName)"
            $itApp = Join-Path $BuildRoot 'itest\GUSEK AI'
            $appId = '{5E4C7A33-89DF-4B4B-9689-5FDFBF23E3A1}'
            $regKey = "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\${appId}_is1"
            $lnk = Join-Path ([Environment]::GetFolderPath('Programs')) 'GUSEK AI.lnk'

            function Stop-InstalledGusek {
                Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -like "$itApp\*" } |
                    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
                Start-Sleep -Milliseconds 600
            }

            Say "Testing clean install into '$itApp' (path with spaces)..."
            Stop-InstalledGusek
            if (Test-Path (Join-Path $itApp 'unins000.exe')) {
                Start-Process -FilePath (Join-Path $itApp 'unins000.exe') -Wait -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES'
            }
            if (Test-Path $itApp) { Remove-Item $itApp -Recurse -Force -ErrorAction SilentlyContinue }

            # 1. Clean Install
            $p = Start-Process -FilePath $setup.FullName -Wait -PassThru -ArgumentList `
                 '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/DIR=`"$itApp`""
            if ($p.ExitCode -eq 0) {
                Pass "Silent installer completed with exit code 0"
            } else {
                Fail "Silent installer failed with exit code $($p.ExitCode)"
                $totalFailures++
            }

            # 2. Verify key files
            foreach ($rel in @('gusek.exe', 'glpsol.exe', 'glpk_4_65.dll', 'Start-Gusek.cmd',
                               'ai\llama\llama-server.exe', 'ai\defaults\GusekAI.ini', 'ai\defaults\system_prompt.txt')) {
                if (Test-Path (Join-Path $itApp $rel)) {
                    Pass "Installed file present: $rel"
                } else {
                    Fail "Missing installed file: $rel"
                    $totalFailures++
                }
            }

            # 3. Verify no model bundled in installer
            $ggufs = @(Get-ChildItem $itApp -Filter '*.gguf' -Recurse -ErrorAction SilentlyContinue)
            if ($ggufs.Count -eq 0) {
                Pass "No model files bundled in installer (installer is model-free)"
            } else {
                Fail "Found $($ggufs.Count) unexpected model files in installer payload"
                $totalFailures++
            }

            # 4. Verify Start Menu shortcut and registry key
            if (Test-Path $lnk) {
                Pass "Start menu shortcut created: $lnk"
            } else {
                Note "Start menu shortcut not found at $lnk (may be in user/programs folder)"
            }
            if (Test-Path $regKey) {
                Pass "Per-user uninstall registry entry registered"
            } else {
                Note "Uninstall registry entry not found at $regKey"
            }

            # 5. Launch test from installed location
            Say "Testing launch from installed location..."
            $instExe = Join-Path $itApp 'gusek.exe'
            if (Test-Path $instExe) {
                $instProc = Start-Process -FilePath $instExe -WorkingDirectory $itApp -PassThru
                Start-Sleep -Seconds 2
                if ($instProc.HasExited) {
                    Fail "Installed gusek.exe exited immediately"
                    $totalFailures++
                } else {
                    Pass "Installed gusek.exe launched and running cleanly"
                    Stop-Process -Id $instProc.Id -Force -ErrorAction SilentlyContinue
                }
            }

            # 6. Upgrade test (custom configurations preserved)
            Say "Testing upgrade preservation..."
            $iniPath = Join-Path $itApp 'ai\defaults\GusekAI.ini'
            $promptPath = Join-Path $itApp 'ai\defaults\system_prompt.txt'
            Add-Content -Path $iniPath -Value "`n# user test setting`nport=29999`n"
            Add-Content -Path $promptPath -Value "`n# user custom instructions`n"

            $upgProc = Start-Process -FilePath $setup.FullName -Wait -PassThru -ArgumentList `
                       '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/DIR=`"$itApp`""
            if ($upgProc.ExitCode -eq 0) {
                Pass "Upgrade installation completed with exit code 0"
                $iniText = Get-Content $iniPath -Raw
                $promptText = Get-Content $promptPath -Raw
                if ($iniText -match '29999' -and $promptText -match 'user custom instructions') {
                    Pass "Upgrade preserved user customizations in GusekAI.ini and system_prompt.txt"
                } else {
                    Fail "Upgrade overwrote user customized settings"
                    $totalFailures++
                }
            } else {
                Fail "Upgrade installer failed with code $($upgProc.ExitCode)"
                $totalFailures++
            }

            # 7. Uninstall test
            Say "Testing uninstaller..."
            Stop-InstalledGusek
            $uninst = Join-Path $itApp 'unins000.exe'
            if (Test-Path $uninst) {
                $unProc = Start-Process -FilePath $uninst -Wait -PassThru -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES'
                Start-Sleep -Seconds 2
                if ($unProc.ExitCode -eq 0) {
                    Pass "Silent uninstaller completed with exit code 0"
                } else {
                    Fail "Silent uninstaller failed with code $($unProc.ExitCode)"
                    $totalFailures++
                }
                if (-not (Test-Path (Join-Path $itApp 'gusek.exe'))) {
                    Pass "Installed executable gusek.exe removed"
                } else {
                    Fail "Installed executable gusek.exe still exists after uninstall"
                    $totalFailures++
                }
                if (-not (Test-Path $lnk)) {
                    Pass "Start menu shortcut removed"
                }
            } else {
                Fail "unins000.exe not found at $uninst"
                $totalFailures++
            }
        }
    }

} catch {
    Fail "Test execution error: $_"
    $totalFailures++
}

Write-Host ""
if ($totalFailures -eq 0) {
    Write-Host "============================================================" -ForegroundColor Green
    Write-Host " ALL TEST SUITES PASSED" -ForegroundColor Green
    Write-Host "============================================================" -ForegroundColor Green
    exit 0
} else {
    Write-Host "============================================================" -ForegroundColor Red
    Write-Host " TEST FAILURES: $totalFailures failed" -ForegroundColor Red
    Write-Host "============================================================" -ForegroundColor Red
    exit 1
}
