# Solo netcode rig: two game instances on this machine playing each other over loopback, with optional simulated loss
# and latency on the second one, each writing its own -netlog. No second person needed.
#   .\scripts\solo-net.ps1                               # host 127.0.0.1 + peer 127.0.0.2, no impairment
#   .\scripts\solo-net.ps1 -Loss 4 -LatAvg 50 -LatNoise 20   # peer drops 4% of incoming packets, +50 ms +-20 ms jitter
#   .\scripts\solo-net.ps1 -Stop                         # kill both instances
# Then, by hand: in window A (host) go Multiplayer > LAN, create a game; window B should list it (lobby unicast to -lanpeer);
# join, start, play a few minutes, quit both. Logs: gameDir\logs\net-solo-A.log / net-solo-B.log; digest with
#   python -I scripts\netlog-summary.py <gameDir>\logs\net-solo-A.log <gameDir>\logs\net-solo-B.log
# Impairment is applied by the engine itself (Transport.cpp loss/latency simulation, -packetloss/-lat* flags); clumsy is not needed.
param(
  [int]$Loss = 0, [int]$LatAvg = 0, [int]$LatAmp = 0, [int]$LatPeriod = 0, [int]$LatNoise = 0,
  [int]$Width = 1600, [int]$Height = 900,
  [switch]$Stop, [switch]$NoCopyExe
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$local = Get-Content "$repo\.zh-local.json" | ConvertFrom-Json
$game = $local.gameDir

if ($Stop) { Get-Process generalszh -ErrorAction SilentlyContinue | Stop-Process -Force; Write-Host "both instances stopped"; exit 0 }
if (Get-Process generalszh -ErrorAction SilentlyContinue) { throw "generalszh.exe is already running; -Stop first" }

if (-not $NoCopyExe) {
  $exe = Get-ChildItem "$repo\build" -Recurse -Include generalszh.exe | Where-Object FullName -match Release | Sort-Object LastWriteTime -Desc | Select-Object -First 1
  if ($exe) { Copy-Item $exe.FullName "$game\generalszh.exe" -Force }
}
New-Item -ItemType Directory -Force "$game\logs" | Out-Null
Remove-Item "$game\logs\net-solo-A.log", "$game\logs\net-solo-B.log" -ErrorAction SilentlyContinue

$common = "-win -nologo -multiinstance -xres $Width -yres $Height -lanpeer 127.0.0.1 -lanpeer 127.0.0.2"
$a = "$common -lanip 127.0.0.1 -netlog logs/net-solo-A.log"
$b = "$common -lanip 127.0.0.2 -netlog logs/net-solo-B.log"
if ($Loss -gt 0)     { $b += " -packetloss $Loss" }
if ($LatAvg -gt 0)   { $b += " -latAvg $LatAvg" }
if ($LatAmp -gt 0)   { $b += " -latAmp $LatAmp" }
if ($LatPeriod -gt 0){ $b += " -latPeriod $LatPeriod" }
if ($LatNoise -gt 0) { $b += " -latNoise $LatNoise" }

Write-Host "A (host): generalszh.exe $a"
Start-Process -FilePath "$game\generalszh.exe" -ArgumentList $a -WorkingDirectory $game | Out-Null
Start-Sleep 6   # let A claim instance 1 (mutex) before B starts
Write-Host "B (peer): generalszh.exe $b"
Start-Process -FilePath "$game\generalszh.exe" -ArgumentList $b -WorkingDirectory $game | Out-Null
Write-Host "Both running. Host a LAN game in A, join from B, play, quit. Then: python -I scripts\netlog-summary.py `"$game\logs\net-solo-A.log`" `"$game\logs\net-solo-B.log`""
