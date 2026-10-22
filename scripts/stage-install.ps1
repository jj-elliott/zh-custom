# Stage (or re-stage) the playable dev install: a mirror of the Steam Zero Hour folder, minus third-party add-ons,
# plus our freshly built generalszh.exe. Regenerates assets.sha256 so the launcher's manifest matches this folder.
#   .\scripts\stage-install.ps1                       # Steam path + dev dir from .zh-local.json (or defaults)
#   .\scripts\stage-install.ps1 -SteamZHPath "<folder containing generals.exe>" -DevDir "C:\Games\ZH\ZeroHour-Dev"
#   .\scripts\stage-install.ps1 -NoManifest           # skip regenerating assets.sha256
#
# The result is "vanilla Steam + our exe". Anything that was in the dev dir but is not in the Steam folder is removed
# (robocopy /MIR), so re-running this is the way back to a known state. Add-ons we ship deliberately (DXVK, config/)
# are applied by the release zip / launcher, not here.
#
# Why the exclusions: the Steam copy on this machine had GenPatcher + GenTool applied. GenTool's d3d8.dll hooks the retail
# exe at fixed addresses and crashes any rebuilt exe (see tests/out/crash-* postmortems). The numbered *.big files are
# GenPatcher UI/asset mods; both machines must have an identical .big set, so the baseline is vanilla. Steam's
# "Verify integrity" restores modified originals but never deletes extras, hence this list.
param(
  [string]$SteamZHPath = "",
  [string]$DevDir = "",
  [switch]$NoManifest,
  [switch]$NoAddons,              # skip layering the verified add-ons from addons\addons.json (Control Bar Pro/HD, ...)
  [switch]$NoDxvk,                # skip DXVK + dxvk.conf (releases ship it; the dev copy should match unless bisecting a visual bug)
  [string[]]$ExcludeFiles = @(
    "d3d8.dll", "d3d8.cfg", "d3d9.dll", "dxvk.conf",          # GenTool proxy / any wrapper left in the Steam folder
    "GenToolUpdater.exe", "game.dat-VISION.log", "EdgeScroller.exe",
    "CustomContentMaps.big", "generalszh.exe", "*.bak",      # stale rebuilt exe, GenPatcher backups of originals
    "0??_*.big", "1??_*.big", "2??_*.big", "3??_*.big", "4??_*.big",
    "5??_*.big", "6??_*.big", "7??_*.big", "8??_*.big", "9??_*.big"   # GenPatcher's priority-numbered add-on archives
  ),
  [string[]]$ExcludeDirs = @("launcher", "crashdumps", "logs"),  # GenLauncher residue; our own runtime folders
  [string[]]$PreserveFiles = @("ZHLauncher.exe", "ZHLauncher.exe.old", "launcher.json", "generalszh.exe.prev", "*.log")  # ours in the dev copy: never mirrored from Steam, never purged
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$localPath = Join-Path $repo ".zh-local.json"
$local = if (Test-Path $localPath) { Get-Content $localPath | ConvertFrom-Json } else { $null }

if (-not $SteamZHPath) { $SteamZHPath = $local.steamDir }
if (-not $SteamZHPath) {
  $SteamZHPath = @(
    "C:\Program Files (x86)\Steam\steamapps\common\Command & Conquer Generals - Zero Hour",
    "C:\Program Files (x86)\Steam\steamapps\common\Command and Conquer Generals - Zero Hour\Command and Conquer Generals Zero Hour",
    "C:\Program Files (x86)\Steam\steamapps\common\Command and Conquer Generals - Zero Hour"
  ) | Where-Object { Test-Path (Join-Path $_ "generals.exe") } | Select-Object -First 1
}
if (-not $SteamZHPath -or -not (Test-Path (Join-Path $SteamZHPath "generals.exe"))) { throw "Steam Zero Hour folder not found; pass -SteamZHPath '<folder containing generals.exe>'" }
if (-not $DevDir) { $DevDir = $local.gameDir }
if (-not $DevDir) { $DevDir = "C:\Games\ZH\ZeroHour-Dev" }

$exe = Get-ChildItem -Path (Join-Path $repo "build") -Recurse -Include "generalszh.exe" -ErrorAction SilentlyContinue |
       Where-Object { $_.FullName -match "Release" } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $exe) { throw "generalszh.exe not found under build\; run scripts\build.ps1 first" }

Write-Host "Staging $DevDir`n  from  $SteamZHPath`n  exe   $($exe.FullName)"
New-Item -ItemType Directory -Force $DevDir | Out-Null

# Mirror. Excluded names are neither copied nor deleted by /MIR, so purge them from the destination explicitly below.
# /XF names are neither copied nor deleted by /MIR: ExcludeFiles are purged explicitly below, PreserveFiles are left alone.
$rcArgs = @($SteamZHPath, $DevDir, "/MIR", "/NFL", "/NDL", "/NJH", "/NJS", "/NP", "/R:2", "/W:2", "/XF") + $ExcludeFiles + $PreserveFiles + @("/XD") + $ExcludeDirs
& robocopy @rcArgs | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }   # 0-7 = success variants
$removed = @()
foreach ($pat in $ExcludeFiles) { Get-ChildItem $DevDir -File -Filter $pat -ErrorAction SilentlyContinue | ForEach-Object { $removed += $_.Name; Remove-Item $_.FullName -Force } }
foreach ($d in $ExcludeDirs) { $p = Join-Path $DevDir $d; if (Test-Path $p) { $removed += "$d\"; Remove-Item $p -Recurse -Force } }
if ($removed) { Write-Host "Removed from dev copy: $($removed -join ', ')" }

# Layer our deliberate add-ons on top of vanilla (the /XF list above strips any copies that came with the Steam folder).
# They only get here after sha256 verification, so the dev copy matches what the release zip will carry.
if (-not $NoAddons) {
  & (Join-Path $PSScriptRoot "fetch-addons.ps1") -Out $DevDir -Quiet
  if ($LASTEXITCODE -ne 0) { Write-Warning "some add-ons are not in the cache; dev copy is missing them (see fetch-addons.ps1)" }
  else { Write-Host "Add-ons applied: $((Get-ChildItem $DevDir -File | Where-Object Name -match '^\d{3}_.*\.big$' | ForEach-Object Name) -join ', ')" }
}

# Same renderer and shipped settings as a release (the /XF list strips any stale copies first).
if (-not $NoDxvk) { & (Join-Path $PSScriptRoot "fetch-dxvk.ps1") -Out $DevDir }
$config = Join-Path $repo "config"
if (Test-Path $config) { Copy-Item (Join-Path $config "*") $DevDir -Recurse -Force }

Copy-Item $exe.FullName (Join-Path $DevDir "generalszh.exe") -Force
$pdb = Join-Path $exe.DirectoryName "generalszh.pdb"
if (Test-Path $pdb) { Copy-Item $pdb (Join-Path $DevDir "generalszh.pdb") -Force }   # lets cdb symbolize dumps from this folder too
$hash = git -C $repo rev-parse --short HEAD
Set-Content (Join-Path $DevDir "BUILD_VERSION.txt") ("dev-local " + $hash + " " + (Get-Date -Format s))

@{ gameDir = $DevDir; steamDir = $SteamZHPath; exe = "generalszh.exe"; buildDir = (Join-Path $repo "build") } |
  ConvertTo-Json | Set-Content $localPath

# Anything third-party still lurking? Flag DLL/EXE names that are not part of a vanilla install.
# Top level only: MSS\ holds Miles Sound System plugins (*.m3d, *.flt, *.asi) and RedistInstallers\ an EA helper, all vanilla.
$vanillaBins = "BINKW32.DLL","DebugWindow.dll","P2XDLL.DLL","ParticleEditor.dll","mss32.dll","patchw32.dll","steam_api.dll",
               "generals.exe","generalszh.exe","WorldBuilder.exe"
$suspects = Get-ChildItem $DevDir -File | Where-Object { $_.Extension -in ".dll",".exe",".asi" -and $vanillaBins -notcontains $_.Name } | Select-Object -ExpandProperty Name
if ($suspects) { Write-Warning "Non-vanilla binaries in dev copy: $($suspects -join ', ')" }

if (-not $NoManifest) {
  & (Join-Path $PSScriptRoot "make-asset-manifest.ps1") -GamePath $DevDir -Out (Join-Path $repo "assets.sha256")
  $changed = git -C $repo diff --stat -- assets.sha256
  if ($changed) { Write-Host "assets.sha256 changed (commit it):`n$changed" } else { Write-Host "assets.sha256 unchanged" }
}
Write-Host "Dev install ready: $DevDir\generalszh.exe ($hash)" -ForegroundColor Green
