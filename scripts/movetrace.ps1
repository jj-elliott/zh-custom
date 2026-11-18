# Movement trace: one scenario run, per-unit jitter, and (with -Unit) the steering decisions of one unit frame by frame.
#   .\scripts\movetrace.ps1 -Scenario desert-mixed                 # per-unit reversals / stalled, worst first
#   .\scripts\movetrace.ps1 -Scenario desert-mixed -Unit 189       # + DBGSTEER / DBGOUT lines for unit 189 in the output file
# The suite says how much; this says which unit and why. Output file path is printed (OUTPUT=).
param(
  [Parameter(Mandatory)][string]$Scenario,
  [int]$Unit = 0,
  [int]$Seed = 1,
  [string]$Tune = "",
  [int]$Level = -1,
  [int]$Top = 8
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$game = (Get-Content "$repo\.zh-local.json" | ConvertFrom-Json).gameDir
$exe = Get-ChildItem "$repo\build" -Recurse -Include generalszh.exe | Where-Object FullName -match Release | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$file = "$repo\tests\courses\$Scenario.scenario"
if (-not (Test-Path $file)) { throw "no such scenario: $file" }
$t = "bench.seed=$Seed"
if ($Unit) { $t += ",debug.unit=$Unit" }
if ($Tune) { $t += ",$Tune" }
$out = "$repo\tests\out\movetrace-$Scenario-$Seed-$Unit.txt"
New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
$a = @("-useCwd", "-headless", "-movebench", "`"$file`"", "-movetune", $t)
if ($Level -ge 0) { $a += @("-moverules", $Level) }
$p = Start-Process -FilePath $exe.FullName -ArgumentList $a -WorkingDirectory $game -PassThru -RedirectStandardOutput $out
if (-not $p.WaitForExit(300000)) { $p.Kill(); throw "timeout" }
$units = Get-Content $out | Where-Object { $_ -like "MoveBenchUnit:*" } | ForEach-Object {
  $r = [ordered]@{}
  foreach ($kv in ($_ -replace "^MoveBenchUnit:\s*", "") -split "\s+") { $k, $v = $kv -split "=", 2; $r[$k] = [double]$v }
  [pscustomobject]$r
}
$units | Sort-Object reversals -Descending | Select-Object -First $Top | ForEach-Object {
  Write-Host ("id={0} vehicle={1} radius={2} reversals={3} revSteer={4} stalled={5}" -f $_.id, $_.vehicle, $_.radius, $_.reversals, $_.revSteer, $_.stalled)
}
Get-Content $out | Where-Object { $_ -like "MoveBench: units=*" } | Write-Host
Write-Host "OUTPUT=$out"
