param([string]$WorkRoot, [string]$RepoRoot)
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$probe = Join-Path $WorkRoot 'wrapper probe with spaces'
New-Item -ItemType Directory -Force -Path (Join-Path $probe 'tests\ai') | Out-Null
Copy-Item (Join-Path $RepoRoot 'gusek.ps1') $probe
Copy-Item (Join-Path $RepoRoot 'gusek.cmd') $probe
$requestedRoot = Join-Path $WorkRoot 'requested-build-root'
$scriptPath = Join-Path $probe 'tests\ai\run-tests.ps1'
foreach ($expectedExit in @(1,0)) {
    $body = @'
param([switch]$Real,[switch]$Gui,[switch]$Installer,[string]$BuildRoot='D:\gusek-build')
Set-Content -LiteralPath (Join-Path $PSScriptRoot 'received-root.txt') -Value $BuildRoot
exit EXPECTED_EXIT
'@
    Set-Content -LiteralPath $scriptPath -Value $body.Replace('EXPECTED_EXIT',[string]$expectedExit) -Encoding ASCII
    & (Join-Path $probe 'gusek.cmd') test -BuildRoot $requestedRoot
    if ($LASTEXITCODE -ne $expectedExit) { throw "Wrapper lost test exit code $expectedExit." }
    if ((Get-Content (Join-Path $probe 'tests\ai\received-root.txt') -Raw).Trim() -ne $requestedRoot) {
        throw 'Wrapper dropped the configured build root.'
    }
    Write-Host "[PASS] Wrapper in a spaced path forwards BuildRoot and exit code $expectedExit."
}

$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $RepoRoot 'gusek.ps1'),[ref]$tokens,[ref]$errors)
$versionFunction = $ast.Find({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Get-NumericVersion'
},$true)
if (-not $versionFunction) { throw 'Version conversion function not found.' }
function Stop-WithError([string]$Message) { throw $Message }
. ([scriptblock]::Create($versionFunction.Extent.Text))
foreach ($case in @(
    @{ Input='0.1.0'; Output='0.1.0.0' },
    @{ Input='v1.76.0-rc1'; Output='1.76.0.0' },
    @{ Input='1.76-ai'; Output='1.76.0.0' },
    @{ Input='2.3.4.5'; Output='2.3.4.5' }
)) {
    if ((Get-NumericVersion $case.Input) -ne $case.Output) { throw 'Wrong numeric Windows version.' }
}
foreach ($invalid in @('65536.1.0','0.1.0/evil','0.1.0-..','999999999999999999999999.0.0')) {
    $rejected = $false
    try { $null = Get-NumericVersion $invalid } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid version accepted: $invalid" }
}
Write-Host '[PASS] Windows version conversion preserves tags and rejects invalid components.'

$releaseVersion = Join-Path $RepoRoot 'scripts\release-version.ps1'
foreach ($case in @(
    @{ Tag='v1.2.3'; Version='1.2.3'; Prerelease='false' },
    @{ Tag='1.2.3'; Version='1.2.3'; Prerelease='false' },
    @{ Tag='v1.2.3-rc.1'; Version='1.2.3-rc.1'; Prerelease='true' }
)) {
    $result = & $releaseVersion -RefType tag -RefName $case.Tag
    if ($result.Version -ne $case.Version -or $result.Prerelease -ne $case.Prerelease) { throw 'Release version conversion failed.' }
}
$result = & $releaseVersion -RefType branch -RefName main -RunNumber 123
if ($result.Version -ne '0.0.0-dev.123' -or $result.Prerelease -ne 'true') { throw 'Manual build version failed.' }
foreach ($invalid in @('v01.2.3','v1.2','v65536.0.0','v1.2.3-..','v1.2.3/evil','v999999999999999999999999.0.0')) {
    $rejected = $false
    try { $null = & $releaseVersion -RefType tag -RefName $invalid } catch { $rejected = $true }
    if (-not $rejected) { throw 'An invalid release tag was accepted.' }
}
Write-Host '[PASS] Release tags, prereleases, manual build versions and invalid tags are handled correctly.'
foreach ($invalidRoot in @('relative-build-root','D:','\drive-relative',[IO.Path]::GetPathRoot($WorkRoot),$RepoRoot,(Join-Path $RepoRoot 'build'))) {
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue' # Native stderr is expected for rejection.
        & (Join-Path $RepoRoot 'gusek.cmd') doctor -BuildRoot $invalidRoot *> (Join-Path $WorkRoot 'invalid-build-root.log')
    } finally { $ErrorActionPreference = $savedPreference }
    if ($LASTEXITCODE -eq 0) { throw 'An unsafe build root was accepted.' }
}
Write-Host '[PASS] Build roots reject relative paths, drive roots and repository overlap.'
exit 0
