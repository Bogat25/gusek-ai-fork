param([string]$BuildRoot, [string]$WorkRoot, [string]$RepoRoot)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
function Assert([bool]$Condition,[string]$Message) {
    if (-not $Condition) { throw $Message }
    Write-Host "[PASS] $Message"
}
function Run-Setup([string]$Executable,[string[]]$Arguments) {
    $process = Start-Process -FilePath $Executable -ArgumentList $Arguments -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Installer process failed: $($process.ExitCode)" }
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class InstallerManifest {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern IntPtr LoadLibraryEx(string file, IntPtr unused, uint flags);
    [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll")] static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32.dll")] static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll")] static extern IntPtr LockResource(IntPtr resource);
    [DllImport("kernel32.dll")] static extern uint SizeofResource(IntPtr module, IntPtr resource);
    public static string Read(string file) {
        IntPtr module = LoadLibraryEx(file, IntPtr.Zero, 2);
        if (module == IntPtr.Zero) throw new Exception("Cannot inspect executable manifest.");
        try {
            IntPtr resource = FindResource(module, new IntPtr(1), new IntPtr(24));
            if (resource == IntPtr.Zero) throw new Exception("Executable has no explicit manifest.");
            byte[] bytes = new byte[SizeofResource(module, resource)];
            Marshal.Copy(LockResource(LoadResource(module, resource)), bytes, 0, bytes.Length);
            return Encoding.UTF8.GetString(bytes);
        } finally { FreeLibrary(module); }
    }
}
'@
function Assert-NoElevation([string]$Executable) {
    $manifest = [InstallerManifest]::Read($Executable)
    Assert ($manifest -match 'requestedExecutionLevel\s+level="asInvoker"' -and
            $manifest -notmatch 'requireAdministrator|highestAvailable') 'Executable manifest requests no elevation.'
}

$Stage = Join-Path $BuildRoot 'stage'
$Dist = Join-Path $BuildRoot 'dist'
$Cache = Join-Path $BuildRoot 'cache'
$InnoSetup = ''
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $RepoRoot 'gusek.ps1'),[ref]$tokens,[ref]$errors)
foreach ($functionName in @('Find-InnoSetup','Get-NumericVersion')) {
    $definition = $ast.Find({
        param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -eq $functionName
    },$true)
    if (-not $definition) { throw "Missing build helper $functionName." }
    . ([scriptblock]::Create($definition.Extent.Text))
}
function Stop-WithError([string]$Message) { throw $Message }
$compiler = Find-InnoSetup
if (-not $compiler) { throw 'Requested installer test needs Inno Setup.' }
$production = Get-ChildItem -LiteralPath $Dist -Filter 'gusek-ai-*-setup.exe' |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
Assert ([bool]$production) 'Production installer exists.'
$version = $production.BaseName.Substring('gusek-ai-'.Length)
$version = $version.Substring(0,$version.Length - '-setup'.Length)
Assert ($production.VersionInfo.FileVersion.Trim() -eq (Get-NumericVersion $version)) 'Installer metadata matches its version.'
$checksum = ((Get-Content -LiteralPath ($production.FullName + '.sha256') -Raw) -split '\s+')[0]
Assert ((Get-FileHash -LiteralPath $production.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -eq $checksum) 'Published installer checksum matches.'
Assert-NoElevation $production.FullName
Assert-NoElevation (Join-Path $Stage 'gusek.exe')
$elevated = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
Write-Host "Installer test caller elevated: $elevated"

# Same packaging source, but unique identity, shortcuts and explicit data path.
# Neither the real installation nor the user's GusekAI model folder is touched.
$identifier = [guid]::NewGuid().ToString('D')
$appId = '{' + $identifier + '}'
$name = 'GUSEK AI Test ' + $identifier
$caseRoot = Join-Path $WorkRoot ('installer-' + $identifier)
$userPrograms = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs'
$itApp = Join-Path $userPrograms $name
$data = Join-Path $caseRoot 'isolated data with spaces'
$outputDir = Join-Path $caseRoot 'output'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) ($name + '.lnk')
$registration = 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + $appId + '_is1'
Assert (-not (Test-Path $shortcut) -and -not (Test-Path $registration)) 'Test identity is separate from existing installations.'
Assert (-not (Test-Path -LiteralPath $itApp)) 'Default per-user test destination is unused.'
$arguments = @(
    '/DAppVersion=0.0.0-test', '/DNumericVersion=0.0.0.0',
    "/DAppIdValue={$appId", "/DAppName=$name", "/DAiDataDir=$data",
    "/DStageDir=$Stage", "/DOutputDir=$outputDir", (Join-Path $RepoRoot 'packaging\gusek-ai.iss')
)
& $compiler @arguments *> (Join-Path $caseRoot 'compile.log')
if ($LASTEXITCODE -ne 0) {
    Get-Content (Join-Path $caseRoot 'compile.log') -Tail 15
    throw 'Isolated installer compilation failed.'
}
$setup = Join-Path $outputDir 'gusek-ai-0.0.0-test-setup.exe'
$uninstaller = Join-Path $itApp 'unins000.exe'
$savedData = $env:GUSEK_AI_DATA
$env:GUSEK_AI_DATA = $data
$uninstalled = $false
try {
    Assert-NoElevation $setup
    Run-Setup $setup @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART')
    Assert (Test-Path -LiteralPath $itApp) 'Default installation uses LocalAppData Programs without a directory override.'
    Assert-NoElevation $uninstaller
    foreach ($relative in @('gusek.exe','glpsol.exe','glpk_4_65.dll','Start-Gusek.cmd',
                            'ai\llama\llama-server.exe','ai\defaults\GusekAI.ini',
                            'ai\defaults\system_prompt.txt','COPYING','AI-README.md','THIRD-PARTY.md',
                            'licenses\scintilla-LICENSE.txt','licenses\scite-LICENSE.txt','licenses\llama.cpp-LICENSE.txt',
                            'ai\llama\LICENSE-LLVM-OpenMP')) {
        Assert (Test-Path (Join-Path $itApp $relative)) "Installed component $relative exists."
    }
    Assert (@(Get-ChildItem -LiteralPath $itApp -Filter '*.gguf' -Recurse).Count -eq 0) 'Installer includes no large model files.'
    Assert (Test-Path $registration) 'Separate per-user uninstall registration exists.'
    Assert (Test-Path $shortcut) 'Separate Start menu shortcut exists.'
    foreach ($machineKey in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\',
                              'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\')) {
        Assert (-not (Test-Path ($machineKey + $appId + '_is1'))) 'Installation creates no machine-wide uninstall registration.'
    }
    $shell = New-Object -ComObject WScript.Shell
    $link = $shell.CreateShortcut($shortcut)
    Assert ($link.TargetPath -eq (Join-Path $itApp 'gusek.exe') -and $link.WorkingDirectory -eq $itApp) 'Per-user shortcut launches the installed app from its own folder.'
    [Runtime.InteropServices.Marshal]::FinalReleaseComObject($link) | Out-Null
    [Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell) | Out-Null
    & (Join-Path $RepoRoot 'tests\ai\launcher-regressions.ps1') -Stage $itApp -WorkRoot $caseRoot -Shortcut $shortcut
    $python = (Get-Command python).Source
    & $python (Join-Path $RepoRoot 'tests\ai\gui_regressions.py') --stage $itApp --work-root $caseRoot --fixtures (Join-Path $WorkRoot 'native')
    if ($LASTEXITCODE -ne 0) { throw 'Installed application regression checks failed.' }

    $ini = Join-Path $itApp 'ai\defaults\GusekAI.ini'
    $prompt = Join-Path $itApp 'ai\defaults\system_prompt.txt'
    Add-Content -LiteralPath $ini -Value '# preserve-test-setting'
    Add-Content -LiteralPath $prompt -Value '# preserve-test-prompt'
    $modelDir = Join-Path $data 'models'
    $notes = Join-Path $data 'context\course.txt'
    New-Item -ItemType Directory -Force -Path $modelDir,(Split-Path $notes) | Out-Null
    $owned = @('Qwen3.5-4B-Q4_K_M.gguf','Qwen3.5-4B-Q4_K_M.gguf.part',
               'Qwen3.5-4B-mmproj-F16.gguf','Qwen3.5-4B-mmproj-F16.gguf.part')
    foreach ($file in $owned) { Set-Content -LiteralPath (Join-Path $modelDir $file) -Value 'owned fixture' }
    $custom = @('student-custom.gguf','student-custom.gguf.part')
    foreach ($file in $custom) { Set-Content -LiteralPath (Join-Path $modelDir $file) -Value 'custom fixture' }
    Set-Content -LiteralPath $notes -Value 'coursework fixture'
    $userConfig = Join-Path $data 'GusekAI.ini'
    Set-Content -LiteralPath $userConfig -Value '# keep user settings'
    $userPrompt = Join-Path $data 'system_prompt.txt'
    $historyDir = Join-Path $data 'history'
    New-Item -ItemType Directory -Force -Path $historyDir | Out-Null
    $userHistory = Join-Path $historyDir 'chat.txt'
    Set-Content -LiteralPath $userPrompt -Value 'user prompt fixture'
    Set-Content -LiteralPath $userHistory -Value 'user history fixture'
    Run-Setup $setup @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/DIR=`"$itApp`"")
    Assert ((Get-Content -LiteralPath $ini -Raw) -match 'preserve-test-setting') 'Upgrade preserves customized defaults.'
    Assert ((Get-Content -LiteralPath $prompt -Raw) -match 'preserve-test-prompt') 'Upgrade preserves customized prompt.'
    foreach ($file in ($owned + $custom)) {
        Assert (Test-Path (Join-Path $modelDir $file)) "Upgrade preserves $file."
    }

    Run-Setup $uninstaller @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART')
    $uninstalled = $true
    Assert (-not (Test-Path (Join-Path $itApp 'gusek.exe'))) 'Uninstall removes installed application.'
    Assert (-not (Test-Path $shortcut) -and -not (Test-Path $registration)) 'Uninstall removes only the test registration and shortcut.'
    foreach ($file in $owned) {
        Assert (-not (Test-Path (Join-Path $modelDir $file))) "Uninstall removes owned $file."
    }
    foreach ($file in $custom) {
        Assert ((Get-Content -LiteralPath (Join-Path $modelDir $file) -Raw).Trim() -eq 'custom fixture') "Uninstall preserves custom $file."
    }
    Assert ((Get-Content -LiteralPath $notes -Raw).Trim() -eq 'coursework fixture') 'Uninstall preserves coursework.'
    Assert ((Get-Content -LiteralPath $userConfig -Raw).Trim() -eq '# keep user settings') 'Uninstall preserves user settings.'
    Assert ((Get-Content -LiteralPath $userPrompt -Raw).Trim() -eq 'user prompt fixture') 'Uninstall preserves custom prompt.'
    Assert ((Get-Content -LiteralPath $userHistory -Raw).Trim() -eq 'user history fixture') 'Uninstall preserves saved history.'
} finally {
    $env:GUSEK_AI_DATA = $savedData
    if (-not $uninstalled -and (Test-Path -LiteralPath $uninstaller)) {
        Run-Setup $uninstaller @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART')
    }
}
exit 0
