# Soak the movement rules on recorded games: the check for a sim change nobody has played yet.
#   .\scripts\soak.ps1                 # every replay in tests\soak and tests\replays, at the build's newest rules level
#   .\scripts\soak.ps1 -Level 0        # at another level
#   .\scripts\soak.ps1 -SkipBuild
#
# A replay is a list of commands plus the CRCs of the game it was recorded in. Under changed movement rules the CRCs
# stop matching within a minute or two, which is expected; "-replaysoak" keeps the simulation running to the end of
# the recording anyway and prints the final logic CRC. Each replay is run twice. Pass = both runs finish, no crash
# dump appears, and the two final CRCs are identical (the sim is deterministic). It says nothing about how it plays.
# Recordings in tests\soak are inputs only: they were made under rules that no longer exist and are not baselines.
param([int]$Level = -1, [switch]$SkipBuild, [int]$TimeoutSec = 600)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$local = Get-Content "$repo\.zh-local.json" | ConvertFrom-Json
$game = $local.gameDir
if (-not $SkipBuild) { & "$PSScriptRoot\build.ps1"; if ($LASTEXITCODE -ne 0) { throw "build failed" } }
$exe = Get-ChildItem "$repo\build" -Recurse -Include generalszh.exe | Where-Object FullName -match Release | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $exe) { throw "generalszh.exe not found" }
$replays = @(Get-ChildItem "$repo\tests\soak\*.rep", "$repo\tests\replays\*.rep" -ErrorAction SilentlyContinue)
if (-not $replays) { Write-Warning "no replays in tests\soak or tests\replays"; exit 0 }

$out = "$repo\tests\out\soak-$(Get-Date -Format yyyyMMdd-HHmmss)"
New-Item -ItemType Directory -Force $out | Out-Null
$dumpDir = "$env:USERPROFILE\Documents\Command and Conquer Generals Zero Hour Data\CrashDumps"
$dumpsBefore = @(Get-ChildItem "$dumpDir\*.dmp" -ErrorAction SilentlyContinue).Count
$fail = 0
foreach ($r in $replays) {
  $runs = @()
  foreach ($i in 1, 2) {
    $o = "$out\$($r.BaseName)-$i.txt"
    $a = @("-useCwd", "-headless", "-replaysoak")
    if ($Level -ge 0) { $a += @("-moverules", $Level) }
    $a += @("-replay", "`"$($r.FullName)`"")
    $p = Start-Process -FilePath $exe.FullName -ArgumentList $a -WorkingDirectory $game -PassThru -RedirectStandardOutput $o
    if (-not $p.WaitForExit($TimeoutSec * 1000)) { $p.Kill(); $runs += "TIMEOUT" } else { $runs += ((Get-Content $o | Select-String "^Soak:") -join "") }
  }
  $ok = $runs[0] -and $runs[0] -ne "TIMEOUT" -and $runs[0] -eq $runs[1]
  if (-not $ok) { $fail++ }
  $c = if ($ok) { "Green" } else { "Red" }
  Write-Host ("{0,-10} {1,-22} {2}" -f $(if ($ok) { "ok" } else { "FAIL" }), $r.Name, $(if ($ok) { $runs[0] } else { "run1: $($runs[0])  run2: $($runs[1])" })) -ForegroundColor $c
}
$newDumps = @(Get-ChildItem "$dumpDir\*.dmp" -ErrorAction SilentlyContinue).Count - $dumpsBefore
if ($newDumps -gt 0) { $fail++; Write-Host "CRASHED: $newDumps new dump(s); run postmortem.ps1" -ForegroundColor Red }
Write-Host "failures: $fail"
Write-Host "OUTPUT_DIR=$out"
exit $fail
