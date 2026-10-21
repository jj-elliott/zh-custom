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
if ($code -ne 0) { Write-Host "BUILD FAILED (exit $code); full log: $log" -ForegroundColor Red } else { Write-Host "build ok; log: $log" }
exit $code
