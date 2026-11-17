# The agent's inner loop for sim changes: build, then replay-check every baseline replay with the headless engine.
#   .\scripts\verify.ps1                        # build + all replays in tests\replays
#   .\scripts\verify.ps1 -Replay foo.rep        # one replay (name in tests\replays, or any path)
#   .\scripts\verify.ps1 -SkipBuild             # reuse the last build
#   .\scripts\verify.ps1 -Import -Name mygame   # copy the newest replay from the game's Replays folder into tests\replays\mygame.rep
# "<replay>.args" beside a replay holds the sim-affecting engine arguments it was recorded with (today "-moverules <n>",
# the Phase 7 movement rules level). -Import writes it and replays run with it; no file = engine defaults.
#
# How it works (upstream mechanism, see docs\AGENT-TODO.md "Findings"): a replay embeds the logic CRC every REPLAY_CRC_INTERVAL
# frames. `generalszh.exe -headless -replay <file>` re-simulates it with the current exe and compares; any divergence prints
# "CRC Mismatch in Frame N" to stdout and the process exits 1. So the replay file IS the baseline: record it with a build whose
# sim you trust, commit it under tests\replays, and every later build must reproduce it bit for bit.
# Consequences: a deliberate sim change (Phase 7 movement, upstream merges touching logic) must re-record baselines (-Import).
# The replay's map must exist (stock maps are in the .big files; custom ones go in the user's Maps folder).
#
# The exe under test is the one in build\ (run with -useCwd so the game folder stays the working directory), so nothing is copied
# over the installed exe and it works while a game is running. -UseInstalledExe tests gameDir\generalszh.exe instead.
param(
  [string]$Replay = "",
  [switch]$SkipBuild,
  [switch]$UseInstalledExe,
  [int]$Jobs = 4,                 # worker processes; 1 = sequential in-process
  [int]$TimeoutSec = 1800,
  [switch]$Import,
  [string]$Name = "",
  [int]$MoveRules = -1            # with -Import: the -moverules level the game was played at (default: the engine's newest)
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$local = Get-Content "$repo\.zh-local.json" | ConvertFrom-Json
$game = $local.gameDir
$userData = "$env:USERPROFILE\Documents\Command and Conquer Generals Zero Hour Data"
$replayDir = "$repo\tests\replays"
New-Item -ItemType Directory -Force $replayDir | Out-Null

if ($Import) {
  # The game overwrites one "last replay" file per session (name comes from GUI:LastReplay; 00000000.rep on this install).
  $src = Get-ChildItem "$userData\Replays\*.rep" -ErrorAction SilentlyContinue | Where-Object Length -gt 0 | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (-not $src) { throw "No replay found in $userData\Replays" }
  if (Get-Process generalszh -ErrorAction SilentlyContinue) { Write-Warning "generalszh.exe is running; the newest replay may still be being written." }
  if (-not $Name) { $Name = "replay-" + $src.LastWriteTime.ToString("yyyyMMdd-HHmmss") }
  $dst = Join-Path $replayDir ($Name + ".rep")
  Copy-Item $src.FullName $dst -Force
  # Pin the movement rules level the game was played at (the engine default = MoveRules::LATEST unless -MoveRules says otherwise),
  # so the baseline keeps verifying after a later Phase 7 step raises the default.
  if ($MoveRules -lt 0) {
    $h = Get-Content "$repo\Core\GameEngine\Include\GameLogic\MoveRules.h" -Raw
    if ($h -match 'LATEST\s*=\s*(\w+)' -and $h -match "$($Matches[1])\s*=\s*(\d+)") { $MoveRules = [int]$Matches[1] } else { throw "cannot read MoveRules::LATEST; pass -MoveRules <n>" }
  }
  Set-Content "$dst.args" "-moverules $MoveRules" -Encoding ascii
  Write-Host "Recorded with: -moverules $MoveRules (tests\replays\$Name.rep.args)"
  Write-Host "Imported $($src.Name) ($([math]::Round($src.Length/1KB)) KB, $($src.LastWriteTime)) -> tests\replays\$Name.rep"
  Write-Host "Now run verify.ps1 -SkipBuild -Replay $Name.rep to confirm it reproduces, then commit it."
  exit 0
}

if (-not $SkipBuild) { & "$PSScriptRoot\build.ps1"; if ($LASTEXITCODE -ne 0) { throw "build failed" } }
$exe = if ($UseInstalledExe) { Get-Item "$game\generalszh.exe" } else {
  Get-ChildItem "$repo\build" -Recurse -Include generalszh.exe | Where-Object FullName -match Release | Sort-Object LastWriteTime -Descending | Select-Object -First 1 }
if (-not $exe) { throw "generalszh.exe not found" }

$replays = if ($Replay) {
  $p = if (Test-Path $Replay) { Get-Item $Replay } elseif (Test-Path "$replayDir\$Replay") { Get-Item "$replayDir\$Replay" } else { throw "replay not found: $Replay" }
  @($p)
} else { @(Get-ChildItem "$replayDir\*.rep" -ErrorAction SilentlyContinue) }
if (-not $replays) { Write-Warning "No replays in tests\replays. Play a skirmish, then: verify.ps1 -Import -Name <name>"; exit 0 }

$out = "$repo\tests\out\verify-$(Get-Date -Format yyyyMMdd-HHmmss)"
New-Item -ItemType Directory -Force $out | Out-Null
$dumpsBefore = Get-ChildItem "$userData\CrashDumps\*.dmp" -ErrorAction SilentlyContinue | ForEach-Object FullName

# Sim-affecting engine arguments are per replay: "<replay>.args" next to it (one line, e.g. "-moverules 0") holds the
# arguments that replay was recorded with. Replays are grouped by that line; one engine run per group.
$groups = $replays | Group-Object { $a = "$($_.FullName).args"; if (Test-Path $a) { (Get-Content $a -Raw).Trim() } else { "" } }
$results = @()
$exit = 0
$elapsed = 0
$run = 0
foreach ($g in $groups) {
  $run++
  $extra = if ($g.Name) { @($g.Name -split '\s+') } else { @() }
  $argList = @("-useCwd", "-headless", "-jobs", $Jobs) + $extra
  foreach ($r in $g.Group) { $argList += @("-replay", "`"$($r.FullName)`"") }
  Write-Host "exe:     $($exe.FullName) ($($exe.LastWriteTime))"
  Write-Host "replays: $($g.Count) ($(($g.Group | ForEach-Object Name) -join ', '))  args: $(if ($g.Name) { $g.Name } else { '(none)' })"
  $started = Get-Date
  $p = Start-Process -FilePath $exe.FullName -ArgumentList $argList -WorkingDirectory $game -PassThru `
         -RedirectStandardOutput "$out\stdout-$run.txt" -RedirectStandardError "$out\stderr-$run.txt"
  $exited = $p.WaitForExit($TimeoutSec * 1000)
  if (-not $exited) { $p.Kill(); Get-Process generalszh -ErrorAction SilentlyContinue | Where-Object StartTime -gt $started | Stop-Process -Force -ErrorAction SilentlyContinue }
  else { $p.WaitForExit() }   # .NET quirk: after the timed overload, ExitCode and redirected output are only guaranteed after the untimed one
  $elapsed += [int]((Get-Date) - $started).TotalSeconds
  $runExit = if ($exited) { $p.ExitCode } else { "TIMEOUT after ${TimeoutSec}s" }
  if ($exited -and $null -eq $runExit) {
    # PS 5.1 + Start-Process -PassThru sometimes never populates ExitCode; read the private field (same trick upstream CI uses).
    $p.HasExited | Out-Null
    $runExit = $p.GetType().GetField('exitCode', 'NonPublic, Instance').GetValue($p)
  }
  $stdout = Get-Content "$out\stdout-$run.txt" -ErrorAction SilentlyContinue
  $stdout | Add-Content "$out\stdout.txt"

  # Per-replay verdicts from the engine's stdout. With -jobs > 1 each worker's output is prefixed "i/n ".
  $runResults = @()
  $current = $null
  foreach ($line in $stdout) {
    $l = $line -replace '^\d+/\d+ ', ''
    if ($l -match '^Simulating Replay "(.+)"') { $current = [pscustomobject]@{ Replay = [IO.Path]::GetFileName($Matches[1]); Status = "ok"; Detail = "" }; $runResults += $current; continue }
    if ($null -eq $current) { continue }
    if ($l -match '^CRC Mismatch in Frame (\d+)') { if ($current.Status -eq "ok") { $current.Status = "CRC MISMATCH"; $current.Detail = "frame $($Matches[1])" } }
    elseif ($l -match '^Cannot open replay') { $current.Status = "CANNOT OPEN"; $current.Detail = "missing file or map?" }
    elseif ($l -match '^Elapsed Time: (\S+) Game Time: (\S+)/(\S+)') { if (-not $current.Detail) { $current.Detail = "game $($Matches[3]) in $($Matches[1])" } }
    elseif ($l -match '^Error!') { if ($current.Status -eq "ok") { $current.Status = "ERROR"; $current.Detail = "worker exit code != 0 (crash?)" } }
  }
  if ($runExit -ne 0 -and @($runResults | Where-Object Status -ne "ok").Count -eq 0) {
    $runResults += [pscustomobject]@{ Replay = "(process, run $run)"; Status = "EXIT $runExit"; Detail = "engine exit code nonzero with no per-replay failure parsed; read stdout-$run.txt" }
  }
  if ($runExit -ne 0) { $exit = $runExit }
  $results += $runResults
}
foreach ($r in $replays) { if (-not ($results | Where-Object Replay -eq $r.Name)) { $results += [pscustomobject]@{ Replay = $r.Name; Status = "NOT RUN"; Detail = "no output for this replay" } } }

$fail = @($results | Where-Object Status -ne "ok").Count
$results | ForEach-Object { $c = if ($_.Status -eq "ok") { "Green" } else { "Red" }; Write-Host ("{0,-14} {1,-50} {2}" -f $_.Status, $_.Replay, $_.Detail) -ForegroundColor $c }
$results | Format-Table -AutoSize | Out-String | Set-Content "$out\summary.txt"
Add-Content "$out\summary.txt" "exe: $($exe.FullName)`nexit: $exit`nelapsed: ${elapsed}s"

$newDumps = Get-ChildItem "$userData\CrashDumps\CrashM*.dmp" -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notin $dumpsBefore }
if ($newDumps) { Write-Host "CRASHED during replay check; running postmortem" -ForegroundColor Red; & "$PSScriptRoot\postmortem.ps1" -Dump ($newDumps | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName }

Write-Host "exit: $exit  elapsed: ${elapsed}s  failures: $fail"
Write-Host "OUTPUT_DIR=$out"
exit $fail
