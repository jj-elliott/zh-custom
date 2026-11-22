# Movement benchmark suite: every scripted scenario in tests\courses, for one or more configurations, side by side.
#   .\scripts\movesuite.ps1                                         # the current build's defaults, plus retail
#   .\scripts\movesuite.ps1 -Configs "queue.contact=1.1","steer.ease=0.15,steer.horizon=36"
#   .\scripts\movesuite.ps1 -NoRetail -Seeds 5 -Detail
#
# One line per configuration: each metric averaged over all scenarios (and over -Seeds variants of each). This is the
# number to move when changing the movement system; movebench.ps1 explains the metrics and the score. A scenario is a
# text file (stock map, units to spawn, where to send them: see MoveBench.cpp), so adding a course needs no map
# editor, no save and nobody at the keyboard. -Detail also prints the per-scenario rows.
param(
  [string[]]$Configs = @(),       # -movetune strings; the defaults are always run first
  [int]$Seeds = 3,
  [switch]$NoRetail,
  [switch]$Detail,
  [switch]$SkipBuild
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $SkipBuild) { & "$PSScriptRoot\build.ps1" | Out-Null; if ($LASTEXITCODE -ne 0) { throw "build failed" } }
$scenarios = @(Get-ChildItem "$repo\tests\courses\*.scenario" | ForEach-Object BaseName)
if (-not $scenarios) { throw "no scenarios in tests\courses" }

$metrics = "score", "meanArrive", "maxArrive", "overlap", "reversals", "stalled", "detour", "timeout", "ovMM", "ovMS", "ovCross", "wobble", "steerPct", "pushPct"
$runs = @([pscustomobject]@{ name = "level 2 (default)"; tune = ""; level = -1 })
foreach ($c in $Configs) { $runs += [pscustomobject]@{ name = $c; tune = $c; level = -1 } }
if (-not $NoRetail) { $runs += [pscustomobject]@{ name = "RETAIL"; tune = ""; level = 0 } }

$summary = @()
$detailRows = @()
foreach ($run in $runs) {
  $rows = @()
  foreach ($sc in $scenarios) {
    $a = @{ Scenario = $sc; Seeds = $Seeds; SkipBuild = $true; PassThru = $true }
    if ($run.tune) { $a.Tune = $run.tune }
    if ($run.level -ge 0) { $a.Level = $run.level }
    $r = & "$PSScriptRoot\movebench.ps1" @a | Select-Object -First 1
    if ($r.error) { throw "$sc ($($run.name)): $($r.error)" }
    $rows += $r
    $d = [ordered]@{ config = $run.name; scenario = $sc }
    foreach ($m in $metrics) { $d[$m] = $r.$m }
    $detailRows += [pscustomobject]$d
  }
  $s = [ordered]@{ config = $run.name }
  foreach ($m in $metrics) { $s[$m] = [math]::Round(($rows | Measure-Object -Property $m -Average).Average, 2) }
  $summary += [pscustomobject]$s
}
# Fixed-width rows written by hand: Format-Table drops columns when the host is narrow.
function Show-Rows($rows, [string[]]$lead) {
  $cols = $lead + $metrics
  Write-Host (($cols | ForEach-Object { if ($lead -contains $_) { "{0,-22}" -f $_ } else { "{0,10}" -f $_ } }) -join " ")
  foreach ($r in $rows) {
    Write-Host (($cols | ForEach-Object { if ($lead -contains $_) { "{0,-22}" -f ([string]$r.$_).Substring(0, [math]::Min(22, ([string]$r.$_).Length)) } else { "{0,10}" -f $r.$_ } }) -join " ")
  }
  Write-Host ""
}
if ($Detail) { Show-Rows $detailRows @("config", "scenario") }
Show-Rows $summary @("config")
Write-Host "scenarios: $($scenarios -join ', ')   variants each: $Seeds"
