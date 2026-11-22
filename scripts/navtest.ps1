# Navigation mesh builder, tested on its own (no engine, ~10 s): compiles Core\...\NavMeshBuild.cpp with
# tests\navmesh\navtest.cpp and runs it.
#   .\scripts\navtest.ps1                         # fuzz: 3000 random grids x 4 tolerances, full validation; then 1000 maps with bridges
#   .\scripts\navtest.ps1 -Fuzz 20000 -Seed 7
#   .\scripts\navtest.ps1 -Scenario river-cross [-Tol 1/1]   # that scenario's map as the built game classifies it
#                                                 # (headless run with -movetune navmesh.dump=1) -> statistics + SVG
#   .\scripts\navtest.ps1 -Grid tests\out\navtest\navgrid-x.txt [-Tol 1/1]   # a grid dumped earlier
#   ... -Radius n    with -Scenario / -Grid: draw the mesh a unit of radius n half-cells uses (ground shrunk by its
#                    radius) and a path across the map on it; prints search time
param([int]$Fuzz = 3000, [int]$Seed = 12345, [string]$Grid = "", [string]$Tol = "1/1", [string]$Scenario = "", [int]$Radius = -1)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$out = Join-Path $repo "tests\out\navtest"
New-Item -ItemType Directory -Force $out | Out-Null
$vcvars = Get-ChildItem "C:\Program Files*\Microsoft Visual Studio\2022\*\VC\Auxiliary\Build\vcvars32.bat" | Select-Object -First 1
if (-not $vcvars) { throw "vcvars32.bat not found" }
$exe = Join-Path $out "navtest.exe"
$src = @("$repo\tests\navmesh\navtest.cpp", "$repo\Core\GameEngine\Source\GameLogic\AI\NavMeshBuild.cpp")
$src += "$repo\Core\GameEngine\Source\GameLogic\AI\NavMeshQuery.cpp"
$cl = "cl /nologo /O2 /W3 /EHsc /DNAVMESH_STANDALONE /D_CRT_SECURE_NO_WARNINGS /I`"$repo\Core\GameEngine\Include`" /Fo`"$out\\`" /Fe`"$exe`" `"$($src[0])`" `"$($src[1])`" `"$($src[2])`""
$env:Path += ";C:\Program Files (x86)\Microsoft Visual Studio\Installer"
$ErrorActionPreference = "Continue"
cmd /c "call `"$($vcvars.FullName)`" >nul && $cl 2>&1"
if ($LASTEXITCODE -ne 0) { Write-Host "navtest: COMPILE FAILED" -ForegroundColor Red; exit 1 }
if ($Scenario) {
  $game = (Get-Content "$repo\.zh-local.json" | ConvertFrom-Json).gameDir
  $gameExe = Get-ChildItem "$repo\build" -Recurse -Include generalszh.exe | Where-Object FullName -match Release |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1
  $file = "$repo\tests\courses\$Scenario.scenario"
  if (-not (Test-Path $file)) { throw "no such scenario: $file" }
  $log = Join-Path $out "run-$Scenario.txt"
  $a = @("-useCwd", "-headless", "-multiinstance", "-movebench", "`"$file`"", "-movetune", "navmesh.dump=1")
  $p = Start-Process -FilePath $gameExe.FullName -ArgumentList $a -WorkingDirectory $game -PassThru -RedirectStandardOutput $log
  if (-not $p.WaitForExit(300000)) { $p.Kill(); throw "timeout" }
  $lines = Get-Content $log
  $lines | Where-Object { $_ -like "NavMesh:*" } | Select-Object -First 1 | ForEach-Object { Write-Host "engine  $_" }
  $header = $lines | Where-Object { $_ -like "NavGrid:*" } | Select-Object -First 1
  if (-not $header) { throw "the run printed no NavGrid (see $log)" }
  $h = [int](($header -split "\s+")[2])
  $rows = $lines | Where-Object { $_ -like "NavGridRow:*" } | Select-Object -First $h | ForEach-Object { $_ -replace "^NavGridRow:\s*", "" }
  $Grid = Join-Path $out "navgrid-$Scenario.txt"
  @(($header -replace "^NavGrid:\s*", "")) + $rows | Set-Content -Encoding ascii $Grid
}
if ($Grid) {
  $parts = $Tol.Split("/")
  $svg = Join-Path $out ((Split-Path $Grid -Leaf) -replace "\.txt$", "")
  $svg = "$svg-tol$($parts[0])_$($parts[1])$(if ($Radius -ge 0) { "-r$Radius" }).svg"
  & $exe svg (Resolve-Path $Grid).Path $svg $parts[0] $parts[1] $Radius
  $code = $LASTEXITCODE
  Write-Host "SVG=$svg"
  exit $code
}
& $exe fuzz $Fuzz $Seed
$code = $LASTEXITCODE
# Bridges (several meshes joined into one atlas): a third as many maps, each built for three unit sizes, exact and simplified.
& $exe bridges ([math]::Max(1, [int]($Fuzz / 3))) $Seed
if ($LASTEXITCODE -ne 0) { $code = $LASTEXITCODE }
exit $code
