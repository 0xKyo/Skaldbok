# Windows build helper: finds Visual Studio, loads the compiler environment, configures and builds with Ninja.
#   scripts\build.ps1                 -> Release build in build\release
#   scripts\build.ps1 -Config Debug   -> Debug build in build\debug (keeps the console window)
#   scripts\build.ps1 -Test           -> also runs the data-layer tests
param(
    [ValidateSet('Release', 'Debug')] [string]$Config = 'Release',
    [switch]$Test,
    [switch]$Clean,
    [switch]$Headless,      # build only the tests (no window, no ImGui): quick check of the data layer
    [string]$Name = ''      # build folder name under build\ (default: the config name); use another one while the app is running
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw 'Visual Studio (with the C++ workload) is required.' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio installation with the C++ tools was found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { $cmake = 'cmake' }

$folder = if ($Name) { $Name } else { $Config.ToLower() }
$build = Join-Path $root ("build\" + $folder)
if ($Clean -and (Test-Path $build)) { Remove-Item $build -Recurse -Force }

$app = if ($Headless) { 'OFF' } else { 'ON' }
$steps = "`"$vcvars`" >nul && `"$cmake`" -S `"$root`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=$Config -DGM_BUILD_APP=$app && `"$cmake`" --build `"$build`""
if ($Test) { $steps += " && ctest --test-dir `"$build`" --output-on-failure" }
cmd /c $steps
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
Write-Host "`nBuilt: $build\skaldbok.exe"
