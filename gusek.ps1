<#
.SYNOPSIS
    Build, test, run and package GUSEK with the local AI assistant.

.DESCRIPTION
    One script for the whole cycle. Mirrors source into a space-free build tree
    (default D:\gusek-build\tree) and builds Scintilla and GUSEK (Sc1) with MSVC x86.

    Everyday use:
        .\gusek.cmd dev          rebuild gusek.exe, then start GUSEK
        .\gusek.cmd test         run unit and protocol tests

    First time:
        .\gusek.cmd fetch        download/cache llama.cpp and models
        .\gusek.cmd full         complete build of Scintilla and GUSEK

    All commands:
        doctor    check prerequisites and toolchains
        fetch     download/cache pinned assets (-NoModel skips large GGUFs)
        full      complete build of Scintilla and GUSEK
        quick     rebuild GUSEK only (fast path)
        run       start built GUSEK
        dev       quick, then run
        test      run tests (-Real, -Gui, -Installer)
        package   assemble portable ZIP in dist
        installer build Inno Setup installer
        clean     clean build tree and outputs
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('doctor', 'fetch', 'full', 'quick', 'run', 'dev', 'test',
                 'package', 'installer', 'deploy', 'clean')]
    [string]$Command = 'doctor',

    # Space-free build root directory
    [string]$BuildRoot = 'D:\gusek-build',

    # Path to vcvarsall.bat (auto-detected when empty)
    [string]$VcVars = '',

    # Parallel jobs / make flags
    [int]$Jobs = 0,

    # Skip downloading multi-GB models in fetch
    [switch]$NoModel,

    # Test switches
    [switch]$Real,
    [switch]$Gui,
    [switch]$Installer,

    # Deploy drive, e.g. E:
    [string]$Drive = '',

    # Version string
    [string]$Version = '1.76-ai',

    # Inno Setup compiler ISCC.exe
    [string]$InnoSetup = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# ---------------------------------------------------------------------
# Pinned Assets
# ---------------------------------------------------------------------
$LlamaZip    = 'llama-b11153-bin-win-cpu-x64.zip'
$LlamaUrl    = "https://github.com/ggml-org/llama.cpp/releases/download/b11153/$LlamaZip"
$LlamaSha256 = '569d19826f3fb00a3fc2df7bd68ab9ad33e5c0d6d69ce022b24372700cee7931'

$ModelFile   = 'Qwen3.5-4B-Q4_K_M.gguf'
$ModelUrl    = "https://huggingface.co/unsloth/Qwen3.5-4B-GGUF/resolve/e87f176479d0855a907a41277aca2f8ee7a09523/$ModelFile"
$ModelSha256 = '00fe7986ff5f6b463e62455821146049db6f9313603938a70800d1fb69ef11a4'
$ModelSize   = 2740937888

$VisionFile   = 'Qwen3.5-4B-mmproj-F16.gguf'
$VisionUrl    = 'https://huggingface.co/unsloth/Qwen3.5-4B-GGUF/resolve/e87f176479d0855a907a41277aca2f8ee7a09523/mmproj-F16.gguf'
$VisionSha256 = 'cd88edcf8d031894960bb0c9c5b9b7e1fea6ebee02b9f7ce925a00d12891f864'
$VisionSize   = 672423616

$InnoFile = 'innosetup-7.1.0-x64.exe'
$InnoUrl = "https://github.com/jrsoftware/issrc/releases/download/is-7_1_0/$InnoFile"
$InnoSha256 = '0362a383ed217d4c4239b5933866dd96d3eb2102737da92f80f6057a4b40df2f'

# ---------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------
$Repo    = $PSScriptRoot
if ($BuildRoot -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^\\]+\\[^\\]+)') { throw 'BuildRoot must be absolute.' }
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot).TrimEnd('\')
$repoPath = [IO.Path]::GetFullPath($Repo).TrimEnd('\')
if ($BuildRoot -eq [IO.Path]::GetPathRoot($BuildRoot).TrimEnd('\') -or
    $BuildRoot -eq $repoPath -or
    $BuildRoot.StartsWith($repoPath + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $repoPath.StartsWith($BuildRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'BuildRoot must be separate from the source repository and cannot be a drive root.'
}
$Tree    = Join-Path $BuildRoot 'tree'
$Cache   = Join-Path $BuildRoot 'cache'
$Logs    = Join-Path $BuildRoot 'logs'
$Dist    = Join-Path $BuildRoot 'dist'
$Stage   = Join-Path $BuildRoot 'stage'
$script:LastLog = $null

if ($BuildRoot -match '\s') {
    Write-Host "BuildRoot must not contain spaces: $BuildRoot" -ForegroundColor Red
    exit 2
}

# ---------------------------------------------------------------------
# Output Helpers
# ---------------------------------------------------------------------
function Say([string]$Message)  { Write-Host "==> $Message" -ForegroundColor Cyan }
function Note([string]$Message) { Write-Host "    $Message" }
function Warn([string]$Message) { Write-Host "WARNING: $Message" -ForegroundColor Yellow }

function Stop-WithError([string]$Message) {
    Write-Host ""
    Write-Host "FAILED: $Message" -ForegroundColor Red
    if ($script:LastLog -and (Test-Path $script:LastLog)) {
        Write-Host "--- last log lines ---" -ForegroundColor Yellow
        Get-Content $script:LastLog -Tail 20 | ForEach-Object { Write-Host "  $_" }
        Write-Host "full log: $script:LastLog"
    }
    exit 1
}

# ---------------------------------------------------------------------
# Toolchain Detection
# ---------------------------------------------------------------------
function Find-VcVars {
    if ($VcVars -and (Test-Path $VcVars)) { return $VcVars }
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

function Initialize-VcVars {
    $bat = Find-VcVars
    if (-not $bat) {
        Stop-WithError "Visual Studio vcvarsall.bat not found. Install VS with C++ support or pass -VcVars."
    }
    # Each build runs inside the developer shell. Never dump inherited variables.
    $script:VcVarsPath = $bat
}

function Find-InnoSetup {
    if ($InnoSetup -and (Test-Path $InnoSetup)) { return $InnoSetup }
    $candidates = @(
        (Join-Path $BuildRoot 'tools\innosetup\ISCC.exe'),
        'D:\rgui-build\tools\innosetup\ISCC.exe',
        'D:\rstudio-build\tools\innosetup\ISCC.exe',
        'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
        'C:\Program Files\Inno Setup 6\ISCC.exe',
        'C:\Program Files (x86)\Inno Setup 7\ISCC.exe',
        'C:\Program Files\Inno Setup 7\ISCC.exe',
        'D:\Program Files\Inno Setup 6\ISCC.exe',
        (Join-Path $Cache 'inno\ISCC.exe')
    )
    foreach ($key in 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 7_is1',
                     'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 7_is1',
                     'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 7_is1',
                     'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
                     'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
                     'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1') {
        try {
            $loc = (Get-ItemProperty $key -ErrorAction Stop).InstallLocation
            if ($loc) { $candidates += (Join-Path $loc 'ISCC.exe') }
        } catch { }
    }
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return $c }
    }
    return $null
}

# ---------------------------------------------------------------------
# SHA-256 and Cache Verification
# ---------------------------------------------------------------------
function Verify-Sha256([string]$FilePath, [string]$ExpectedSha) {
    if (-not (Test-Path $FilePath)) { return $false }
    $hash = (Get-FileHash -Path $FilePath -Algorithm SHA256).Hash.ToLowerInvariant()
    return ($hash -eq $ExpectedSha.ToLowerInvariant())
}

function Ensure-CacheFile([string]$TargetName, [string]$Url, [string]$ExpectedSha, [int64]$ExpectedSize) {
    New-Item -ItemType Directory -Force -Path $Cache | Out-Null
    $dest = Join-Path $Cache $TargetName
    if (Test-Path $dest) {
        if (Verify-Sha256 $dest $ExpectedSha) {
            Note "cache ok: $TargetName"
            return $dest
        }
        Warn "cache mismatch for $TargetName; re-checking..."
    }

    # Check peer caches to avoid multi-GB re-download
    $peerCaches = @('D:\rgui-build\cache', 'D:\rstudio-build\cache')
    foreach ($pc in $peerCaches) {
        $candidate = Join-Path $pc $TargetName
        if (Test-Path $candidate) {
            if (Verify-Sha256 $candidate $ExpectedSha) {
                Note "reusing verified $TargetName from $pc"
                Copy-Item $candidate $dest -Force
                return $dest
            }
        }
    }

    Say "downloading $TargetName..."
    $part = "$dest.part"
    $client = New-Object System.Net.WebClient
    $client.DownloadFile($Url, $part)
    if (-not (Verify-Sha256 $part $ExpectedSha)) {
        Remove-Item $part -Force -ErrorAction SilentlyContinue
        Stop-WithError "SHA-256 mismatch for $TargetName downloaded from $Url"
    }
    Move-Item -Path $part -Destination $dest -Force
    Note "download verified: $TargetName"
    return $dest
}

# ---------------------------------------------------------------------
# Source Synchronization
# ---------------------------------------------------------------------
function Sync-Tree {
    New-Item -ItemType Directory -Force -Path $Tree | Out-Null
    $out = & robocopy.exe $Repo $Tree /E /XD .git .vs .vscode /XF *.obj *.pdb *.ilk *.idb *.exp *.lib *.exe *.dll *.res *.log /NDL /NJH /NJS /NP /NC /NS /R:1 /W:1 /MT:16
    $rc = $LASTEXITCODE
    if ($rc -ge 8) { Stop-WithError "copying sources to $Tree failed (robocopy code $rc)" }
    Note "source mirror synchronized to $Tree"
}

# ---------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------
function Do-Doctor {
    Say "GUSEK Doctor Check"
    $vcvars = Find-VcVars
    if ($vcvars) {
        Note "MSVC vcvarsall: $vcvars"
        Initialize-VcVars
        $clVer = (cmd /d /c "call `"$script:VcVarsPath`" x86 >nul 2>&1 && cl 2>&1" | Select-Object -First 1)
        Note "Compiler: $clVer"
    } else {
        Warn "MSVC vcvarsall.bat NOT found"
    }

    $glpsol = Join-Path $Repo 'glpsol.exe'
    if (Test-Path $glpsol) {
        $ver = (& $glpsol --version | Select-Object -First 1)
        Note "GLPK Solver: $glpsol ($ver)"
        $testMod = Join-Path $Repo 'examples\transp.mod'
        if (Test-Path $testMod) {
            $chk = (& $glpsol --check --math $testMod 2>&1)
            if ($LASTEXITCODE -eq 0) {
                Note "Solver check on transp.mod: PASSED"
            } else {
                Warn "Solver check on transp.mod returned code $LASTEXITCODE"
            }
        }
    } else {
        Warn "glpsol.exe not found at repo root"
    }

    $inno = Find-InnoSetup
    if ($inno) { Note "Inno Setup: $inno" } else { Note "Inno Setup: not found (installer build will skip or fetch)" }

    # Cached models check
    foreach ($m in @(
        @{ Name = $LlamaZip; Sha = $LlamaSha256 },
        @{ Name = $ModelFile; Sha = $ModelSha256 },
        @{ Name = $VisionFile; Sha = $VisionSha256 }
    )) {
        $p = Join-Path $Cache $m.Name
        $found = $false
        if ((Test-Path $p) -and (Verify-Sha256 $p $m.Sha)) {
            Note "Cache $($m.Name): present and valid"
            $found = $true
        } else {
            foreach ($pc in @('D:\rgui-build\cache', 'D:\rstudio-build\cache')) {
                $cand = Join-Path $pc $m.Name
                if ((Test-Path $cand) -and (Verify-Sha256 $cand $m.Sha)) {
                    Note "Cache $($m.Name): available in $pc"
                    $found = $true
                    break
                }
            }
        }
        if (-not $found) { Note "Cache $($m.Name): not cached yet (run .\gusek.cmd fetch)" }
    }
}

function Get-InnoSetupVersion([string]$Compiler) {
    # Older ISCC releases reject --version on stderr. With ErrorAction=Stop,
    # Windows PowerShell raises NativeCommandError even when stderr is redirected.
    # An unsupported probe must fall back to our pinned installer.
    try {
        $output = (& $Compiler --version 2>$null | Out-String).Trim()
        if ($LASTEXITCODE -eq 0 -and $output -match '^\d+\.\d+\.\d+$') {
            return $output
        }
    } catch { }
    return $null
}

function Install-InnoSetup {
    $compiler = Join-Path $BuildRoot 'tools\innosetup\ISCC.exe'
    if (Test-Path -LiteralPath $compiler) {
        if ((Get-InnoSetupVersion $compiler) -ne '7.1.0') {
            Stop-WithError 'Cached compiler is not Inno Setup 7.1.0; select a fresh BuildRoot.'
        }
        return $compiler
    }
    $existing = Find-InnoSetup
    if ($existing -and ((Get-InnoSetupVersion $existing) -eq '7.1.0')) {
        $target = Split-Path $compiler -Parent
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        Copy-Item -Path (Join-Path (Split-Path $existing -Parent) '*') -Destination $target -Recurse -Force
        return $compiler
    }
    $setup = Ensure-CacheFile $InnoFile $InnoUrl $InnoSha256 0
    $toolDirectory = Split-Path $compiler -Parent
    $process = Start-Process -FilePath $setup -ArgumentList @(
        '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CURRENTUSER','/NOICONS',"/DIR=""$toolDirectory"""
    ) -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $compiler) -or
        (Get-InnoSetupVersion $compiler) -ne '7.1.0') {
        Stop-WithError 'Could not install the pinned Inno Setup compiler for the current user.'
    }
    return $compiler
}

function Do-Fetch {
    Say "Fetching pinned assets into $Cache"
    Ensure-CacheFile $LlamaZip $LlamaUrl $LlamaSha256 0 | Out-Null
    if (-not $NoModel) {
        Ensure-CacheFile $ModelFile $ModelUrl $ModelSha256 $ModelSize | Out-Null
        Ensure-CacheFile $VisionFile $VisionUrl $VisionSha256 $VisionSize | Out-Null
    } else {
        Note "Skipping model downloads (-NoModel)"
    }
    $null = Install-InnoSetup
    Say "Fetch complete."
}

function Do-FullBuild {
    Say "Full build: Scintilla and GUSEK"
    Initialize-VcVars
    $modeStamp = Join-Path $Logs 'build-mode.txt'
    $releaseMode = 'msvc-x86-release-v1'
    if (-not (Test-Path -LiteralPath $modeStamp) -or
        (Get-Content -LiteralPath $modeStamp -Raw).Trim() -ne $releaseMode) {
        # A previous caller may have inherited DEBUG through nmake's environment.
        # Rebuild the owned mirror before switching its compiler/runtime mode.
        $resolvedTree = [IO.Path]::GetFullPath($Tree)
        if (-not $resolvedTree.StartsWith($BuildRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
            Stop-WithError 'Refusing to rebuild a tree outside BuildRoot.'
        }
        if (Test-Path -LiteralPath $resolvedTree) { Remove-Item -LiteralPath $resolvedTree -Recurse -Force }
    }
    Sync-Tree

    New-Item -ItemType Directory -Force -Path $Logs | Out-Null
    $log = Join-Path $Logs ("build-full-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
    $script:LastLog = $log

    $sw = [System.Diagnostics.Stopwatch]::StartNew()

    Say "Building Scintilla..."
    $scintillaDir = Join-Path $Tree 'scintilla\win32'
    Push-Location $scintillaDir
    try {
        cmd /d /c "call `"$script:VcVarsPath`" x86 >nul 2>&1 && set `"DEBUG=`" && nmake -f scintilla.mak > `"$log`" 2>&1"
        if ($LASTEXITCODE -ne 0) { Stop-WithError "Scintilla build failed" }
    } finally { Pop-Location }

    Say "Building GUSEK (Sc1)..."
    $sciteDir = Join-Path $Tree 'scite\win32'
    Push-Location $sciteDir
    try {
        cmd /d /c "call `"$script:VcVarsPath`" x86 >nul 2>&1 && set `"DEBUG=`" && nmake -f scite.mak ..\bin\Sc1.exe >> `"$log`" 2>&1"
        if ($LASTEXITCODE -ne 0) { Stop-WithError "GUSEK (Sc1.exe) build failed" }
    } finally { Pop-Location }

    $builtExe = Join-Path $Tree 'scite\bin\Sc1.exe'
    if (-not (Test-Path $builtExe)) { Stop-WithError "Sc1.exe was not created" }

    Say "Staging GUSEK into $Stage..."
    New-Item -ItemType Directory -Force -Path $Stage | Out-Null
    Copy-Item $builtExe (Join-Path $Stage 'gusek.exe') -Force

    # Copy runtime dependencies and assets from repo
    foreach ($item in @('glpsol.exe', 'glpk_4_65.dll', 'SciTEGlobal.properties', 'gmpl.properties',
                        'gnuplot.properties', 'python.properties', 'gusek.lua', 'gmpl.api', 'gmpl.abb',
                        'README', 'COPYING', 'AI-README.md', 'THIRD-PARTY.md', 'gusek.html', 'gmpl.pdf', 'glpk.pdf', 'examples', 'gusek_tips', 'Start-Gusek.cmd')) {
        $src = Join-Path $Repo $item
        if (Test-Path $src) {
            Copy-Item $src $Stage -Recurse -Force
        }
    }

    $licenseDir = Join-Path $Stage 'licenses'
    New-Item -ItemType Directory -Force -Path $licenseDir | Out-Null
    Copy-Item -LiteralPath (Join-Path $Repo 'scintilla\License.txt') -Destination (Join-Path $licenseDir 'scintilla-LICENSE.txt') -Force
    Copy-Item -LiteralPath (Join-Path $Repo 'scite\License.txt') -Destination (Join-Path $licenseDir 'scite-LICENSE.txt') -Force
    Copy-Item -LiteralPath (Join-Path $Repo 'packaging\licenses\llama.cpp-LICENSE.txt') -Destination $licenseDir -Force

    # Stage llama-server
    $llamaZipPath = Join-Path $Cache $LlamaZip
    if (Test-Path $llamaZipPath) {
        if (-not (Verify-Sha256 $llamaZipPath $LlamaSha256)) {
            Stop-WithError "Cached llama.cpp archive does not match its SHA-256 pin."
        }
        $llamaDest = Join-Path $Stage 'ai\llama'
        New-Item -ItemType Directory -Force -Path $llamaDest | Out-Null
        Expand-Archive -Path $llamaZipPath -DestinationPath $llamaDest -Force
        Note "staged llama-server in $llamaDest"
    }

    # Stage defaults
    $aiDefaults = Join-Path $Repo 'ai\defaults'
    if (Test-Path $aiDefaults) {
        $stageDefaults = Join-Path $Stage 'ai\defaults'
        New-Item -ItemType Directory -Force -Path $stageDefaults | Out-Null
        Copy-Item (Join-Path $aiDefaults '*') $stageDefaults -Recurse -Force
    }

    $sw.Stop()
    Set-Content -LiteralPath $modeStamp -Value $releaseMode -Encoding ASCII
    Say ("Full build succeeded in {0:0.0}s. Output staged at $Stage" -f $sw.Elapsed.TotalSeconds)
}

function Do-QuickBuild {
    $modeStamp = Join-Path $Logs 'build-mode.txt'
    if (-not (Test-Path -LiteralPath $modeStamp) -or
        (Get-Content -LiteralPath $modeStamp -Raw).Trim() -ne 'msvc-x86-release-v1') {
        Do-FullBuild
        return
    }
    Say "Quick build: GUSEK"
    Initialize-VcVars
    Sync-Tree

    New-Item -ItemType Directory -Force -Path $Logs | Out-Null
    $log = Join-Path $Logs ("build-quick-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
    $script:LastLog = $log

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $sciteDir = Join-Path $Tree 'scite\win32'
    Push-Location $sciteDir
    try {
        cmd /d /c "call `"$script:VcVarsPath`" x86 >nul 2>&1 && set `"DEBUG=`" && nmake -f scite.mak ..\bin\Sc1.exe > `"$log`" 2>&1"
        if ($LASTEXITCODE -ne 0) { Stop-WithError "GUSEK build failed" }
    } finally { Pop-Location }

    $builtExe = Join-Path $Tree 'scite\bin\Sc1.exe'
    New-Item -ItemType Directory -Force -Path $Stage | Out-Null
    Copy-Item $builtExe (Join-Path $Stage 'gusek.exe') -Force
    $sw.Stop()
    Say ("Quick build succeeded in {0:0.0}s" -f $sw.Elapsed.TotalSeconds)
}

function Do-Run {
    $exe = Join-Path $Stage 'gusek.exe'
    if (-not (Test-Path $exe)) {
        Do-FullBuild
    }
    Say "Starting GUSEK from $Stage..."
    $devProfile = Join-Path $BuildRoot 'profile'
    New-Item -ItemType Directory -Force -Path $devProfile | Out-Null
    $devConfig = Join-Path $devProfile 'GusekAI.ini'
    if (-not (Test-Path -LiteralPath $devConfig)) {
        $settings = @()
        if (Test-Path -LiteralPath (Join-Path $Cache $ModelFile)) { $settings += "model=$(Join-Path $Cache $ModelFile)" }
        if (Test-Path -LiteralPath (Join-Path $Cache $VisionFile)) { $settings += "vision_model=$(Join-Path $Cache $VisionFile)" }
        $settings | Set-Content -LiteralPath $devConfig -Encoding UTF8
    }
    $savedData = $env:GUSEK_AI_DATA
    try {
        $env:GUSEK_AI_DATA = $devProfile
        Start-Process -FilePath $exe -WorkingDirectory $Stage
    } finally { $env:GUSEK_AI_DATA = $savedData }
}

function Do-Clean {
    Say "Cleaning build tree at $BuildRoot"
    foreach ($target in @($Tree,$Stage,$Dist)) {
        $resolved = [IO.Path]::GetFullPath($target)
        if (-not $resolved.StartsWith($BuildRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
            Stop-WithError 'Refusing to clean a path outside BuildRoot.'
        }
    }
    if (Test-Path $Tree)  { Remove-Item -LiteralPath $Tree -Recurse -Force }
    if (Test-Path $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force }
    if (Test-Path $Dist)  { Remove-Item -LiteralPath $Dist -Recurse -Force }
    Note "Clean completed."
}

function Get-NumericVersion([string]$Value) {
    if ($Value -notmatch '^v?(\d+(?:\.\d+){1,3})(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?$') {
        Stop-WithError "Invalid version '$Value'. Use a numeric version such as 0.1.0 or 0.1.0-rc1."
    }
    $parts = @($Matches[1].Split('.') | ForEach-Object {
        $partValue = [uint32]0
        if (-not [uint32]::TryParse($_, [ref]$partValue) -or $partValue -gt 65535) {
            Stop-WithError "Windows version components must be between 0 and 65535."
        }
        [string]$partValue
    })
    while ($parts.Count -lt 4) { $parts += '0' }
    return ($parts -join '.')
}

function Do-Installer {
    $numericVersion = Get-NumericVersion $Version
    Do-FullBuild
    $iscc = if ($InnoSetup) { Find-InnoSetup } else { Install-InnoSetup }
    if (-not $iscc) {
        Stop-WithError "Inno Setup compiler (ISCC.exe) not found. Pass -InnoSetup <path>."
    }
    $iss = Join-Path $Repo 'packaging\gusek-ai.iss'
    if (-not (Test-Path $iss)) {
        Stop-WithError "Inno Setup script not found at $iss"
    }

    New-Item -ItemType Directory -Force -Path $Dist | Out-Null
    Say "Compiling Inno Setup installer using $iscc..."
    $proc = Start-Process -FilePath $iscc -ArgumentList @(
        "/DAppVersion=$Version",
        "/DNumericVersion=$numericVersion",
        "/DStageDir=`"$Stage`"",
        "/DOutputDir=`"$Dist`"",
        "`"$iss`""
    ) -Wait -PassThru -NoNewWindow

    if ($proc.ExitCode -ne 0) {
        Stop-WithError "ISCC compilation failed with code $($proc.ExitCode)"
    }

    $setupExe = Join-Path $Dist ("gusek-ai-{0}-setup.exe" -f $Version)
    if (Test-Path $setupExe) {
        $sha = (Get-FileHash -Algorithm SHA256 $setupExe).Hash.ToLowerInvariant()
        Set-Content -Path "$setupExe.sha256" -Value "$sha *$((Get-Item $setupExe).Name)" -Encoding ASCII
        Say "Installer ready: $setupExe"
        Note "Installer SHA256: $sha"
    } else {
        $any = Get-ChildItem $Dist -Filter "gusek-ai-*-setup.exe" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($any) {
            $sha = (Get-FileHash -Algorithm SHA256 $any.FullName).Hash.ToLowerInvariant()
            Set-Content -Path "$($any.FullName).sha256" -Value "$sha *$($any.Name)" -Encoding ASCII
            Say "Installer ready: $($any.FullName)"
        }
    }
}

# ---------------------------------------------------------------------
# Entry Point
# ---------------------------------------------------------------------
switch ($Command) {
    'doctor'    { Do-Doctor }
    'fetch'     { Do-Fetch }
    'full'      { Do-FullBuild }
    'quick'     { Do-QuickBuild }
    'run'       { Do-Run }
    'dev'       { Do-QuickBuild; Do-Run }
    'test'      {
        if (Test-Path (Join-Path $Repo 'tests\ai\run-tests.ps1')) {
            $global:LASTEXITCODE = 0
            & (Join-Path $Repo 'tests\ai\run-tests.ps1') -BuildRoot $BuildRoot -Real:$Real -Gui:$Gui -Installer:$Installer
            exit $LASTEXITCODE
        } else {
            Note "Running unit checks..."
            Do-Doctor
        }
    }
    'package'   {
        $null = Get-NumericVersion $Version
        Do-FullBuild
        Say "Assembling portable distribution in $Dist..."
        New-Item -ItemType Directory -Force -Path $Dist | Out-Null
        $zipDest = Join-Path $Dist ("gusek-{0}-portable.zip" -f $Version)
        Compress-Archive -Path (Join-Path $Stage '*') -DestinationPath $zipDest -Force
        $sha = (Get-FileHash -Algorithm SHA256 $zipDest).Hash.ToLowerInvariant()
        Set-Content -Path "$zipDest.sha256" -Value "$sha *$((Get-Item $zipDest).Name)" -Encoding ASCII
        Note "Portable package ready: $zipDest"
        Note "Portable package SHA256: $sha"
    }
    'installer' {
        Do-Installer
    }
    'clean'     { Do-Clean }
}
