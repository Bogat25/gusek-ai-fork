param([string]$RefType, [string]$RefName, [string]$RunNumber = '0')
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
if ($RefType -eq 'tag') {
    if ($RefName -cnotmatch '^v?((0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?)$') {
        throw 'Use a version tag such as v0.1.0 or v0.2.0-rc1.'
    }
    $version = $Matches[1]
    foreach ($part in (($version -split '-')[0] -split '\.')) {
        $value = [uint32]0
        if (-not [uint32]::TryParse($part, [ref]$value) -or $value -gt 65535) {
            throw 'Version components must fit the Windows installer range 0..65535.'
        }
    }
} else {
    if ($RunNumber -notmatch '^[0-9]+$') { throw 'Invalid workflow run number.' }
    $version = "0.0.0-dev.$RunNumber"
}
$prerelease = if ($version.Contains('-')) { 'true' } else { 'false' }
if (Test-Path Env:GITHUB_OUTPUT) {
    "version=$version" | Out-File -LiteralPath $env:GITHUB_OUTPUT -Encoding utf8 -Append
    "prerelease=$prerelease" | Out-File -LiteralPath $env:GITHUB_OUTPUT -Encoding utf8 -Append
}
[pscustomobject]@{ Version = $version; Prerelease = $prerelease }
