# Build Zero Hour Release/Win32 in-place. Used by bootstrap.ps1, verify.ps1, and the agent.
#   .\scripts\build.ps1            # incremental
#   .\scripts\build.ps1 -Clean
param([switch]$Clean)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ($Clean -and (Test-Path "$repo\build")) { Remove-Item "$repo\build" -Recurse -Force }
$vcvars = Get-ChildItem "C:\Program Files*\Microsoft Visual Studio\2022\*\VC\Auxiliary\Build\vcvars32.bat" | Select-Object -First 1
if (-not $vcvars) { throw "vcvars32.bat not found; VS 2022 Build Tools with C++ workload required" }
# Don't depend on the caller's PATH (agent shells often have a stale one): take the machine+user PATH from the registry and
# add the VS Installer dir, which holds vswhere.exe (CMake's MSVC discovery needs it).
$env:Path = [Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [Environment]::GetEnvironmentVariable("Path","User") +
            ";C:\Program Files (x86)\Microsoft Visual Studio\Installer"
if (Test-Path "$repo\CMakePresets.json") {
  # TheSuperHackers layout: preset does configure+build, deps auto-fetched.
  $cmd = "call `"$($vcvars.FullName)`" && cd /d `"$repo`" && cmake --workflow --preset win32"
} else {
  # Reforged-style layout: plain CMake, SDKs must already be dropped in.
  $cmd = "call `"$($vcvars.FullName)`" && cd /d `"$repo`" && cmake -S GeneralsMD/Code -B build -G `"Visual Studio 17 2022`" -A Win32 && cmake --build build --config Release"
}
# Native tools write warnings/progress to stderr; under $ErrorActionPreference=Stop a redirected stderr line (2>&1 from a caller)
# becomes a terminating error before the build even starts. Success is judged by the exit code alone.
$ErrorActionPreference = "Continue"
$log = Join-Path $repo "tests\out\build-$(Get-Date -Format yyyyMMdd-HHmmss).log"
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
cmd /c "$cmd 2>&1" | Tee-Object -FilePath $log
$code = $LASTEXITCODE
if ($code -ne 0) { Write-Host "BUILD FAILED (exit $code); full log: $log" -ForegroundColor Red; exit $code }
Write-Host "build ok; log: $log"
# Stage the result into the dev install (.zh-local.json gameDir) so a human test sees this build: exe, PDB and config.
# Skipped, with a notice, when the exe there is locked (a game instance is running).
$localCfg = Join-Path $repo ".zh-local.json"
if (Test-Path $localCfg) {
  $local = Get-Content $localCfg -Raw | ConvertFrom-Json
  $game = $local.gameDir
  $exe = Get-ChildItem (Join-Path $repo "build") -Recurse -Filter generalszh.exe -ErrorAction SilentlyContinue |
         Where-Object { $_.FullName -notmatch "CMakeFiles" } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if ($game -and (Test-Path $game) -and $exe) {
    try {
      Copy-Item $exe.FullName (Join-Path $game "generalszh.exe") -Force -ErrorAction Stop
      $pdb = Join-Path $exe.DirectoryName "generalszh.pdb"
      if (Test-Path $pdb) { Copy-Item $pdb (Join-Path $game "generalszh.pdb") -Force }
      $config = Join-Path $repo "config"
      if (Test-Path $config) { Copy-Item (Join-Path $config "*") $game -Recurse -Force }
      Write-Host "staged into $game"
    } catch {
      Write-Host "NOT STAGED into $game (exe locked? a game instance may be running): $($_.Exception.Message)" -ForegroundColor Yellow
    }
  }
}
exit 0
