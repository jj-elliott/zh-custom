# Movement test saves: jump straight into a prepared situation instead of playing a skirmish up to it.
#   .\scripts\movetest.ps1 -Import -Name cliff      # keep the newest save game as tests\saves\cliff.sav
#   .\scripts\movetest.ps1 -Name cliff              # launch the dev build straight into that save
#   .\scripts\movetest.ps1 -Name cliff -Level 6     # same, at an older movement rules level, to compare
#   .\scripts\movetest.ps1 -List
#
# How to make one: in a skirmish, build the army and park it where the test starts (before a choke, a cliff road, a
# base to drive through), then Esc > Save. Run -Import right after.
#
# Why this works across builds: the Phase 7 movement state (steering memory, queue timers) is deliberately not in the
# save format, and the movement rules level is a process setting, not saved. So one save loads at any level.
# Limits: a game started from a save records no replay, so there is nothing for verify.ps1 to import afterwards; this
# is for looking, the skirmish replays are for proof. Saves hold a copy of map data, so tests\saves is git-ignored.
param(
  [string]$Name = "",
  [int]$Level = -1,             # -moverules value; default = the build's newest
  [switch]$Import,
  [switch]$List,
  [string]$ExtraArgs = ""
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$local = Get-Content "$repo\.zh-local.json" | ConvertFrom-Json
$game = $local.gameDir
$saveDir = "$env:USERPROFILE\Documents\Command and Conquer Generals Zero Hour Data\Save"
$store = "$repo\tests\saves"
New-Item -ItemType Directory -Force $store | Out-Null

if ($List) {
  Get-ChildItem "$store\*.sav" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | ForEach-Object { "{0,-24} {1,6} KB  {2}" -f $_.BaseName, [math]::Round($_.Length/1KB), $_.LastWriteTime }
  exit 0
}
if (-not $Name) { throw "give -Name <name> (see -List)" }

if ($Import) {
  $src = Get-ChildItem "$saveDir\*.sav" -ErrorAction SilentlyContinue | Where-Object { $_.Length -gt 1KB -and $_.Name -notlike "movetest-*" } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (-not $src) { throw "No save game found in $saveDir" }
  Copy-Item $src.FullName "$store\$Name.sav" -Force
  Write-Host "Imported $($src.Name) ($([math]::Round($src.Length/1KB)) KB, $($src.LastWriteTime)) -> tests\saves\$Name.sav"
  exit 0
}

$sav = "$store\$Name.sav"
if (-not (Test-Path $sav)) { throw "no such test save: $sav (see -List)" }
if (Get-Process generalszh -ErrorAction SilentlyContinue) { throw "generalszh.exe is already running; close it first" }

# The engine loads -loadsave by file name from its Save folder.
$installed = "movetest-$Name.sav"
New-Item -ItemType Directory -Force $saveDir | Out-Null
Copy-Item $sav "$saveDir\$installed" -Force

# Make sure the game folder has the current build (build.ps1 cannot stage while a game is running).
$exe = Get-ChildItem "$repo\build" -Recurse -Include generalszh.exe | Where-Object FullName -match Release | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($exe -and (Get-FileHash $exe.FullName).Hash -ne (Get-FileHash "$game\generalszh.exe").Hash) {
  Copy-Item $exe.FullName "$game\generalszh.exe" -Force
  $pdb = $exe.FullName -replace '\.exe$', '.pdb'
  if (Test-Path $pdb) { Copy-Item $pdb "$game\generalszh.pdb" -Force }
  Write-Host "staged the current build into $game"
}

$gameArgs = @("-loadsave", $installed)
if ($Level -ge 0) { $gameArgs += @("-moverules", $Level) }
if ($ExtraArgs) { $gameArgs += ($ExtraArgs -split '\s+') }
Write-Host "launching: generalszh.exe $($gameArgs -join ' ')"
Start-Process -FilePath "$game\generalszh.exe" -ArgumentList $gameArgs -WorkingDirectory $game | Out-Null
