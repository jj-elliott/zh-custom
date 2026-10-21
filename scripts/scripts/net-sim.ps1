# Simulate bad network conditions on the local link so netcode can be tested without a second human.
#   .\scripts\net-sim.ps1 -Loss 5 -DelayMs 80 -JitterMs 30     # start
#   .\scripts\net-sim.ps1 -Off                                  # stop
# Uses clumsy (https://jagt.github.io/clumsy/), installed via winget if missing. Needs admin.
# For an actual two-instance test on one machine: run two copies of the game from two folders (-win on both) and
# host/join over 127.0.0.1; clumsy filters loopback when "Capture at loopback" is set, which the filter below includes.
param([int]$Loss = 0, [int]$DelayMs = 0, [int]$JitterMs = 0, [switch]$Off)
$ErrorActionPreference = "Stop"
Get-Process clumsy -ErrorAction SilentlyContinue | Stop-Process -Force
if ($Off) { Write-Host "net-sim off"; exit 0 }
$clumsy = Get-Command clumsy.exe -ErrorAction SilentlyContinue
if (-not $clumsy) {
  winget install --id jagt.clumsy -e --accept-source-agreements --accept-package-agreements | Out-Null
  $clumsy = Get-Command clumsy.exe -ErrorAction SilentlyContinue
  if (-not $clumsy) { throw "clumsy not found on PATH after install; see https://jagt.github.io/clumsy/" }
}
$f = "udp"   # Zero Hour's game traffic is UDP; adjust port filter once the net layer is instrumented
$a = @("--filter", $f)
if ($Loss -gt 0)    { $a += "--drop","on","--drop-chance",$Loss }
if ($DelayMs -gt 0) { $a += "--lag","on","--lag-time",$DelayMs }
if ($JitterMs -gt 0){ $a += "--lag","on","--lag-time",($DelayMs + $JitterMs) }  # clumsy has no true jitter; approximate with a higher ceiling
Start-Process $clumsy.Source -ArgumentList $a -Verb RunAs
Write-Host "net-sim on: loss=$Loss% delay=${DelayMs}ms jitter~${JitterMs}ms (filter: $f)"
