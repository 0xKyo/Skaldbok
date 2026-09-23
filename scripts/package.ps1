# Builds a Release and assembles a portable folder + zip that can be copied to any Windows machine:
#   dist\Skaldbok\skaldbok.exe  skaldbok_web.exe  pack_check.exe  web\  data\packs\core\...  docs\  examples\
# The PDFs are NOT included (copyright); the app only needs the generated data. The app never ships or shows a copy of the
# book's own pages (its "Original: book p.N" links just open the reader's own PDF, in their own PDF viewer).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

& (Join-Path $PSScriptRoot 'build.ps1') -Config Release
if (-not (Test-Path "$root\data\packs\core\rules.json")) { throw 'data\packs\core is missing (the Core pack: the books'' rules, tables, creatures, spells...).' }

$out = Join-Path $root 'dist\Skaldbok'
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force "$out\data" | Out-Null
Copy-Item "$root\build\release\skaldbok.exe" $out
Copy-Item "$root\build\release\pack_check.exe" $out
Copy-Item "$root\build\release\skaldbok_web.exe" $out
# the players' page, if it has been built (cd web && npm install && npm run build)
if (Test-Path "$root\web\client\dist\index.html") { Copy-Item "$root\web\client\dist" "$out\web" -Recurse }
else { Write-Warning 'web\client\dist is missing: the package will not include the players'' page (cd web; npm install; npm run build).' }
Copy-Item "$root\data\packs" "$out\data\packs" -Recurse
Copy-Item "$root\docs" "$out\docs" -Recurse
Copy-Item "$root\examples" "$out\examples" -Recurse
Copy-Item "$root\README.md" $out
Copy-Item "$root\Skaldbok.bat" $out

$zip = Join-Path $root 'dist\Skaldbok.zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $out -DestinationPath $zip
$mb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "`nPackage: $zip ($mb MB)"

