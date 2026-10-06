<#
.SYNOPSIS
    Fresh native, GUI and build-wrapper regression checks using isolated profiles.
.DESCRIPTION
    -Real adds CPU inference through the actual GUSEK pane.
    -Installer compiles an isolated installer identity and tests preservation.
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
$Repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$Stage = Join-Path $BuildRoot 'stage'
$Work = Join-Path $BuildRoot ('tests\run-' + [guid]::NewGuid().ToString('N'))
$Native = Join-Path $Work 'native'
$Profile = Join-Path $Work 'profile'
$TempDir = Join-Path $Work 'tmp'
New-Item -ItemType Directory -Force -Path $Native,$Profile,$TempDir | Out-Null

function Find-VcVars {
    $path = & (Join-Path $Repo 'scripts\find-vcvars.ps1')
    if ($path) { return $path }
    throw 'MSVC C++ tools are required for the native checks.'
}

function Wait-Port([int]$Port) {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $deadline) {
        $tcp = [System.Net.Sockets.TcpClient]::new()
        try { $tcp.Connect('127.0.0.1',$Port); return } catch {} finally { $tcp.Dispose() }
        Start-Sleep -Milliseconds 50
    }
    throw 'Fake server did not start.'
}

$savedEnv = @{}
foreach ($name in @('GUSEK_AI_DATA','SciTE_HOME','SciTE_USERHOME','TEMP','TMP')) {
    $savedEnv[$name] = [Environment]::GetEnvironmentVariable($name,'Process')
}
$env:GUSEK_AI_DATA = $Profile
$env:SciTE_HOME = $Stage
$env:SciTE_USERHOME = $Profile
$env:TEMP = $TempDir
$env:TMP = $TempDir
$exitCode = 0
try {
    $python = (Get-Command python -ErrorAction Stop).Source
    $vcvars = Find-VcVars
    $ai = Join-Path $Repo 'scite\win32\ai'
    $exe = Join-Path $Native 'test_gusek_ai.exe'
    $src = Join-Path $PSScriptRoot 'test_gusek_ai.cxx'
    # Always compile current implementation and headers into isolated outputs.
    $compile = "call `"$vcvars`" x86 >nul 2>&1 && cl /nologo /MTd /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /DWIN32 /D_DEBUG " +
        "/I `"$ai`" `"$src`" `"$ai\GusekAiConfig.cxx`" `"$ai\GusekAiProtocol.cxx`" " +
        "`"$ai\GusekAiImage.cxx`" `"$ai\GusekAiRender.cxx`" `"$ai\GusekAiDownload.cxx`" /Fe`"$exe`" " +
        'ws2_32.lib winhttp.lib bcrypt.lib gdiplus.lib ole32.lib oleaut32.lib shell32.lib advapi32.lib user32.lib gdi32.lib'
    Push-Location $Native
    try {
        & cmd /d /c $compile *> (Join-Path $Work 'native-build.log')
        if ($LASTEXITCODE -ne 0) {
            Get-Content (Join-Path $Work 'native-build.log') -Tail 20
            throw 'Native harness compilation failed.'
        }
    } finally { Pop-Location }

    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback,0)
    $listener.Start()
    $port = $listener.LocalEndpoint.Port
    $listener.Stop()
    $server = Start-Process -FilePath $python -ArgumentList @(
        "`"$(Join-Path $PSScriptRoot 'fakeserver.py')`"",$port
    ) -PassThru -WindowStyle Hidden
    try {
        Wait-Port $port
        & $exe $port $Native | Tee-Object -FilePath (Join-Path $Work 'native-tests.log')
        if ($LASTEXITCODE -ne 0) { throw 'Native regression tests failed.' }
    } finally {
        if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    }

    & (Join-Path $PSScriptRoot 'build-regressions.ps1') -WorkRoot $Work -RepoRoot $Repo
    if ($LASTEXITCODE -ne 0) { throw 'Build-wrapper regressions failed.' }

    if (-not (Test-Path (Join-Path $Stage 'gusek.exe'))) {
        throw "No built GUSEK in $Stage. Run .\gusek.cmd full -BuildRoot `"$BuildRoot`" first."
    }
    & $python (Join-Path $PSScriptRoot 'gui_regressions.py') --stage $Stage --work-root $Work
    if ($LASTEXITCODE -ne 0) { throw 'Native GUI regression checks failed.' }

    if ($Real) {
        $model = $null
        $reader = $null
        foreach ($cachePath in @((Join-Path $BuildRoot 'cache'),'D:\rgui-build\cache','D:\rstudio-build\cache')) {
            $candidate = Join-Path $cachePath 'Qwen3.5-4B-Q4_K_M.gguf'
            if (-not $model -and (Test-Path $candidate)) { $model = $candidate }
            $candidate = Join-Path $cachePath 'Qwen3.5-4B-mmproj-F16.gguf'
            if (-not $reader -and (Test-Path $candidate)) { $reader = $candidate }
        }
        if (-not $model) { throw 'Requested real model check needs the pinned model; run gusek.cmd fetch.' }
        if (-not $reader) { throw 'Requested real vision check needs the pinned projector; run gusek.cmd fetch.' }
        if ((Get-FileHash -LiteralPath $reader -Algorithm SHA256).Hash.ToLowerInvariant() -ne
            'cd88edcf8d031894960bb0c9c5b9b7e1fea6ebee02b9f7ce925a00d12891f864') {
            throw 'Real picture reader does not match its SHA-256 pin.'
        }
        if ((Get-FileHash -LiteralPath $model -Algorithm SHA256).Hash.ToLowerInvariant() -ne
            '00fe7986ff5f6b463e62455821146049db6f9313603938a70800d1fb69ef11a4') {
            throw 'Real model does not match its SHA-256 pin.'
        }
        & $python (Join-Path $PSScriptRoot 'gui_regressions.py') --stage $Stage --work-root $Work --real-model $model --real-reader $reader
        if ($LASTEXITCODE -ne 0) { throw 'Real GUSEK inference/cleanup check failed.' }
    } else {
        Write-Host '[SKIP] Real CPU model checks (use -Real).'
    }

    if ($Installer) {
        & (Join-Path $PSScriptRoot 'installer-regressions.ps1') -BuildRoot $BuildRoot -WorkRoot $Work -RepoRoot $Repo
        if ($LASTEXITCODE -ne 0) { throw 'Installer regressions failed.' }
        $package = Get-ChildItem -LiteralPath (Join-Path $BuildRoot 'dist') -Filter 'gusek-*-portable.zip' |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not $package) { throw 'Distribution checks require a portable package; run gusek.cmd package.' }
        $portable = Join-Path $Work 'portable app with spaces'
        Expand-Archive -LiteralPath $package.FullName -DestinationPath $portable
        $portableExe = Get-ChildItem -LiteralPath $portable -Filter gusek.exe -Recurse | Select-Object -First 1
        if (-not $portableExe) { throw 'Portable ZIP does not include the application.' }
        $portableStage = $portableExe.DirectoryName
        & (Join-Path $PSScriptRoot 'launcher-regressions.ps1') -Stage $portableStage -WorkRoot $Work
        & $python (Join-Path $PSScriptRoot 'gui_regressions.py') --stage $portableStage --work-root $Work --fixtures $Native
        if ($LASTEXITCODE -ne 0) { throw 'Relocated portable GUI regressions failed.' }
    } else {
        Write-Host '[SKIP] Installer checks (use -Installer).'
    }
    Write-Host 'All requested test suites passed.'
} catch {
    Write-Host "FAILED: $_" -ForegroundColor Red
    $exitCode = 1
} finally {
    foreach ($name in $savedEnv.Keys) {
        [Environment]::SetEnvironmentVariable($name,$savedEnv[$name],'Process')
    }
    Write-Host "Test files and logs: $Work"
}
exit $exitCode
