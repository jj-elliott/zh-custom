# Build Zero Hour Release/Win32 in-place. Used by bootstrap.ps1, verify.ps1, and the agent.
#   .\scripts\build.ps1            # incremental
#   .\scripts\build.ps1 -Clean
param([switch]$Clean)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ($Clean -and (Test-Path "$repo\build")) { Remove-Item "$repo\build" -Recurse -Force }
$vcvars = Get-ChildItem "C:\Program Files*\Microsoft Visual Studio\2022\*\VC\Auxiliary\Build\vcvars32.bat" | Select-Object -First 1
if (-not $vcvars) { throw "vcvars32.bat not found; VS 2022 Build Tools with C++ workload required" }
if (Test-Path "$repo\CMakePresets.json") {
  # TheSuperHackers layout: preset does configure+build, deps auto-fetched.
  $cmd = "call `"$($vcvars.FullName)`" && cd /d `"$repo`" && cmake --workflow --preset win32"
} else {
  # Reforged-style layout: plain CMake, SDKs must already be dropped in.
  $cmd = "call `"$($vcvars.FullName)`" && cd /d `"$repo`" && cmake -S GeneralsMD/Code -B build -G `"Visual Studio 17 2022`" -A Win32 && cmake --build build --config Release"
}
cmd /c $cmd
exit $LASTEXITCODE
