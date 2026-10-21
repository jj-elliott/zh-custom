# The agent's inner loop: build, run a headless replay, compare CRC and (optionally) screenshots to a baseline.
#   .\scripts\verify.ps1                       # build + all replays in tests\replays
#   .\scripts\verify.ps1 -Replay foo.rep       # one replay
#   .\scripts\verify.ps1 -Baseline             # (re)record baselines instead of comparing
#   .\scripts\verify.ps1 -SkipBuild
#
# STATUS: SCAFFOLD. The headless invocation and CRC extraction are deferred to the agent
# (docs\AGENT-TODO.md, item 2). Upstream documents the mechanism under replay_testing / crcdiff
# in its wiki; the exact flags and the headless mode must be confirmed against the checked-out source.
param([string]$Replay = "", [switch]$Baseline, [switch]$SkipBuild)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$local = Get-Content "$repo\.zh-local.json" | ConvertFrom-Json
$game = $local.gameDir

if (-not $SkipBuild) { & "$PSScriptRoot\build.ps1"; if ($LASTEXITCODE -ne 0) { throw "build failed" } }
$exe = Get-ChildItem "$repo\build" -Recurse -Include generalszh.exe | ? FullName -match Release | select -First 1
Copy-Item $exe.FullName "$game\generalszh.exe" -Force

$replays = if ($Replay) { @(Get-Item "$repo\tests\replays\$Replay") } else { Get-ChildItem "$repo\tests\replays\*.rep" -ErrorAction SilentlyContinue }
if (-not $replays) { Write-Warning "No replays in tests\replays. Record one (replays are on by default; copy from the game's Replays folder) and re-run."; exit 0 }

$fail = 0
foreach ($r in $replays) {
  $name = $r.BaseName
  $crcOut = "$repo\tests\out\$name.crc"
  New-Item -ItemType Directory -Force "$repo\tests\out" | Out-Null

  # TODO(agent): replace with the real headless invocation. Expected shape, to be confirmed in source:
  #   & "$game\generalszh.exe" -headless -replay "$($r.FullName)" -crclog "$crcOut" -quitAfterReplay
  throw "verify.ps1: headless replay invocation not yet implemented (see docs\AGENT-TODO.md #2)"

  $base = "$repo\tests\baseline\$name.crc"
  if ($Baseline) { New-Item -ItemType Directory -Force "$repo\tests\baseline" | Out-Null; Copy-Item $crcOut $base -Force; Write-Host "baseline recorded: $name"; continue }
  if (-not (Test-Path $base)) { Write-Warning "$name: no baseline; run with -Baseline"; continue }
  $diff = Compare-Object (Get-Content $base) (Get-Content $crcOut)
  if ($diff) { Write-Host "FAIL $name: first divergence at line $(( $diff | select -First 1 ).InputObject)" -ForegroundColor Red; $fail++ }
  else { Write-Host "ok   $name" -ForegroundColor Green }
}
exit $fail
