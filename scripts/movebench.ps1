# Movement benchmark: put numbers on how a group gets through a course, and compare tunings.
#   .\scripts\movebench.ps1                                   # the "pathfinding" test save, automatic course, current build
#   .\scripts\movebench.ps1 -Level 0                          # the same under retail rules
#   .\scripts\movebench.ps1 -Tune "steer.ease=0.15"           # with tuning constants overridden for this run
#   .\scripts\movebench.ps1 -Sweep "steer.ease=0.1,0.15,0.25,0.4"          # one run per value, side by side
#   .\scripts\movebench.ps1 -Course cliff                     # a recorded course (tests\saves\<save>.<course>.course)
#   .\scripts\movebench.ps1 -Record -Course cliff             # LAUNCHES THE GAME into the save; give the move orders
#                                                             # that make up the course, then quit. Needs the human.
#   .\scripts\movebench.ps1 -Scenario desert-cross            # a scripted course (tests\courses\*.scenario): stock map,
#                                                             # spawned units, orders. No save, nobody at the keyboard.
#   .\scripts\movebench.ps1 -ListTunables
#
# What it runs: generalszh.exe -headless -loadsave <save> -movebench <course|auto> [-moverules n] [-movetune ...]
# (GameLogic/MoveBench.h). No window, a couple of seconds a run, and repeatable to the last digit, so a difference
# between two tunings is the tuning.
#
# Columns: meanArrive/maxArrive (s from order to rest), goalDist (how far from the ordered point units ended; large =
# gave up short), overlap (vehicle-pair-seconds more than 40% inside each other), reversals (turn direction flips per
# unit: jitter), stalled (unit-seconds standing still while trying to move), detour (travelled / straight line).
# Each configuration is run on -Seeds variants of the course (another random seed, destination moved a few cells) and
# averaged; "spread" is max - min score across them. Movement is chaotic: differences smaller than the spread are luck.
# score = meanArrive + 0.5*maxArrive + 2*overlap + reversals + 0.5*stalled, lower is better: a single number to sort
# by, not a truth. Change the weights below if they stop matching what feels bad. A tuning that wins here still has
# to be played: the benchmark cannot see whether movement looks right.
param(
  [string]$Save = "pathfinding",
  [string]$Course = "auto",
  [string]$Scenario = "",         # tests\courses\<name>.scenario: a stock map + spawned units + orders; needs no save
  [int]$Level = -1,
  [string]$Tune = "",
  [string]$Sweep = "",
  [switch]$Record,
  [switch]$ListTunables,
  [switch]$PassThru,              # emit the result rows as objects instead of printing a table (used by movesuite.ps1)
  [switch]$SkipBuild,
  [int]$Seeds = 5,                # variants of the course per configuration (bench.seed 1..N); results are averaged
  [int]$TimeoutSec = 600
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$local = Get-Content "$repo\.zh-local.json" | ConvertFrom-Json
$game = $local.gameDir
$saveDir = "$env:USERPROFILE\Documents\Command and Conquer Generals Zero Hour Data\Save"
$scenarioFile = ""
if ($Scenario) {
  $scenarioFile = "$repo\tests\courses\$Scenario.scenario"
  if (-not (Test-Path $scenarioFile)) { throw "no such scenario: $scenarioFile" }
} else {
  $sav = "$repo\tests\saves\$Save.sav"
  if (-not (Test-Path $sav)) { throw "no such test save: $sav (movetest.ps1 -List)" }
  New-Item -ItemType Directory -Force $saveDir | Out-Null
  Copy-Item $sav "$saveDir\movetest-$Save.sav" -Force
}
$installed = "movetest-$Save.sav"
$courseFile = "$repo\tests\saves\$Save.$Course.course"

if ($Record) {
  if ($Course -eq "auto") { throw "give the course a name: -Record -Course <name>" }
  if (Get-Process generalszh -ErrorAction SilentlyContinue) { throw "generalszh.exe is already running; close it first" }
  if (Test-Path $courseFile) { Remove-Item $courseFile }
  Write-Host "launching the game into '$Save'. Give the move orders that make up the course, then quit the game."
  Start-Process -FilePath "$game\generalszh.exe" -ArgumentList @("-loadsave", $installed, "-movebenchrecord", "`"$courseFile`"") -WorkingDirectory $game | Out-Null
  exit 0
}

if (-not $SkipBuild) { & "$PSScriptRoot\build.ps1" | Out-Null; if ($LASTEXITCODE -ne 0) { throw "build failed" } }
$exe = Get-ChildItem "$repo\build" -Recurse -Include generalszh.exe | Where-Object FullName -match Release | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $exe) { throw "generalszh.exe not found" }
$courseArg = if ($Course -eq "auto") { "auto" } else { if (-not (Test-Path $courseFile)) { throw "no such course: $courseFile (record it with -Record -Course $Course)" }; "`"$courseFile`"" }
$out = "$repo\tests\out\movebench-$(Get-Date -Format yyyyMMdd-HHmmss)"
New-Item -ItemType Directory -Force $out | Out-Null

function Invoke-Bench([string]$tune, [string]$label) {
  if ($ListTunables -or $Seeds -le 1) { return Invoke-BenchOnce $tune $label }
  # Average over course variants: movement is chaotic, and one run can differ by luck more than two tunings do.
  $runs = @()
  foreach ($seed in 1..$Seeds) {
    $t = if ($tune) { "$tune,bench.seed=$seed" } else { "bench.seed=$seed" }
    $one = Invoke-BenchOnce $t $label
    if ($one.error) { return $one }
    $runs += $one
  }
  $avg = [ordered]@{ run = $label }
  foreach ($k in "score", "meanArrive", "maxArrive", "goalDist", "overlap", "reversals", "stalled", "detour", "timeout") {
    $avg[$k] = [math]::Round(($runs | Measure-Object -Property $k -Average).Average, 2)
  }
  $scores = $runs | ForEach-Object { $_.score }
  $avg["spread"] = [math]::Round((($scores | Measure-Object -Maximum).Maximum - ($scores | Measure-Object -Minimum).Minimum), 1)
  [pscustomobject]$avg
}

function Invoke-BenchOnce([string]$tune, [string]$label) {
  $a = if ($scenarioFile) { @("-useCwd", "-headless", "-movebench", "`"$scenarioFile`"") } else { @("-useCwd", "-headless", "-loadsave", $installed, "-movebench", $courseArg) }
  if ($Level -ge 0) { $a += @("-moverules", $Level) }
  if ($tune) { $a += @("-movetune", $tune) }
  $o = "$out\run-$((Get-ChildItem $out).Count).txt"
  $p = Start-Process -FilePath $exe.FullName -ArgumentList $a -WorkingDirectory $game -PassThru -RedirectStandardOutput $o
  if (-not $p.WaitForExit($TimeoutSec * 1000)) { $p.Kill(); return [pscustomobject]@{ run = $label; error = "TIMEOUT" } }
  $line = Get-Content $o | Where-Object { $_ -like "MoveBench:*" } | Select-Object -Last 1
  if ($ListTunables) { Get-Content $o | Where-Object { $_ -like "MoveTune:*" } | Sort-Object -Unique | ForEach-Object { Write-Host $_ } }
  if (-not $line -or $line -notmatch "units=") { return [pscustomobject]@{ run = $label; error = "$line" } }
  $r = [ordered]@{ run = $label }
  foreach ($kv in ($line -replace "^MoveBench:\s*", "") -split "\s+") { $k, $v = $kv -split "=", 2; $r[$k] = [double]$v }
  $r["score"] = [math]::Round($r.meanArrive + 0.5 * $r.maxArrive + 2 * $r.overlap + $r.reversals + 0.5 * $r.stalled, 1)
  [pscustomobject]$r
}

if ($ListTunables) { Invoke-Bench "list" "list" | Out-Null; exit 0 }

$rows = @()
if ($Sweep) {
  $name, $values = $Sweep -split "=", 2
  $rows += Invoke-Bench $Tune "(default)"
  foreach ($v in ($values -split ",")) {
    $t = if ($Tune) { "$Tune,$name=$v" } else { "$name=$v" }
    $rows += Invoke-Bench $t "$name=$v"
  }
} else {
  $rows += Invoke-Bench $Tune $(if ($Tune) { $Tune } else { "(default)" })
}
if ($PassThru) { $rows; exit 0 }
$rows | Format-Table run, score, spread, meanArrive, maxArrive, goalDist, overlap, reversals, stalled, detour, timeout, error -AutoSize | Out-String -Width 220 | Write-Host
$rows | Export-Csv "$out\results.csv" -NoTypeInformation
Write-Host "OUTPUT_DIR=$out"
