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
exit 0
