# Drive plan (NavDrive: path string -> arcs and straights a unit can follow exactly), tested on its own, no engine.
#   .\scripts\drivetest.ps1                 # 20000 random paths x 4 real units' numbers, every frame checked
#   .\scripts\drivetest.ps1 -Count 100000 -Seed 7
#   .\scripts\drivetest.ps1 -Svg            # draws one path as each unit drives it -> tests\out\navtest\drive.svg
param([int]$Count = 20000, [int]$Seed = 12345, [switch]$Svg)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$out = Join-Path $repo "tests\out\navtest"
New-Item -ItemType Directory -Force $out | Out-Null
$vcvars = Get-ChildItem "C:\Program Files*\Microsoft Visual Studio\2022\*\VC\Auxiliary\Build\vcvars32.bat" | Select-Object -First 1
if (-not $vcvars) { throw "vcvars32.bat not found" }
$exe = Join-Path $out "drivetest.exe"
$cl = "cl /nologo /O2 /W3 /EHsc /DNAVMESH_STANDALONE /D_CRT_SECURE_NO_WARNINGS /I`"$repo\Core\GameEngine\Include`" /Fo`"$out\\`" /Fe`"$exe`" `"$repo\tests\navmesh\drivetest.cpp`" `"$repo\Core\GameEngine\Source\GameLogic\AI\NavDrive.cpp`" `"$repo\Core\GameEngine\Source\GameLogic\AI\NavMeshBuild.cpp`" `"$repo\Core\GameEngine\Source\GameLogic\AI\NavMeshQuery.cpp`""
$env:Path += ";C:\Program Files (x86)\Microsoft Visual Studio\Installer"
$ErrorActionPreference = "Continue"
cmd /c "call `"$($vcvars.FullName)`" >nul && $cl 2>&1"
if ($LASTEXITCODE -ne 0) { Write-Host "drivetest: COMPILE FAILED" -ForegroundColor Red; exit 1 }
if ($Svg) {
  $file = Join-Path $out "drive.svg"
  & $exe svg $file $Seed
  Write-Host "SVG=$file"
  exit $LASTEXITCODE
}
& $exe $Count $Seed
exit $LASTEXITCODE
