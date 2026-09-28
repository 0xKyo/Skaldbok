# Builds a Release, assembles the portable folder (via package.ps1), then packages it as Skaldbok.rar.
# Requires 7-Zip (C:\Program Files\7-Zip\7z.exe) or WinRAR (C:\Program Files\WinRAR\Rar.exe).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

& (Join-Path $PSScriptRoot 'package.ps1')

$out = Join-Path $root 'dist\Skaldbok'
$rar = Join-Path $root 'dist\Skaldbok.rar'

if (Test-Path $rar) { Remove-Item $rar -Force }

$sevenZip = 'C:\Program Files\7-Zip\7z.exe'
$winRar   = 'C:\Program Files\WinRAR\Rar.exe'

if (Test-Path $sevenZip) {
    & $sevenZip a -trar $rar $out | Out-Null
} elseif (Test-Path $winRar) {
    & $winRar a -r $rar $out | Out-Null
} else {
    throw '7-Zip or WinRAR not found. Install one of them and try again.'
}

$mb = [math]::Round((Get-Item $rar).Length / 1MB, 1)
Write-Host "`nDeployed: $rar ($mb MB)"
