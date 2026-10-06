param([string]$VcVars = '', [string]$VsWhere = '')
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if ($VcVars) {
    if (-not (Test-Path -LiteralPath $VcVars -PathType Leaf)) {
        throw "The specified vcvarsall.bat does not exist: $VcVars"
    }
    return [IO.Path]::GetFullPath($VcVars)
}

if (-not $VsWhere) {
    $VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
}
if (Test-Path -LiteralPath $VsWhere -PathType Leaf) {
    try {
        $paths = @(& $VsWhere -latest -products '*' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -find 'VC\Auxiliary\Build\vcvarsall.bat' 2>$null)
        if ($LASTEXITCODE -eq 0) {
            foreach ($path in $paths) {
                if ($path -and (Test-Path -LiteralPath $path.Trim() -PathType Leaf)) {
                    return [IO.Path]::GetFullPath($path.Trim())
                }
            }
        }
    } catch { }
}

# Keep standard installations usable when vswhere is unavailable.
foreach ($root in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
    foreach ($version in @('18','2022','2019')) {
        foreach ($edition in @('Enterprise','Professional','Community','BuildTools')) {
            $path = Join-Path $root "Microsoft Visual Studio\$version\$edition\VC\Auxiliary\Build\vcvarsall.bat"
            if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
        }
    }
}
return $null
