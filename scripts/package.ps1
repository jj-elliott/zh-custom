# Package a release exactly the same way whether run on this machine (scripts\release.ps1) or in CI (release.yml).
#   .\scripts\package.ps1 -Tag v0.1.1 [-OutDir out] [-SkipLauncher] [-NoDxvk]
# Produces in OutDir:
#   zh-game-<tag>.zip      generalszh.exe (+ any DLLs beside it), add-ons (addons\addons.json), assets.sha256, config\*, DXVK d3d8/d3d9 + dxvk.conf, BUILD_VERSION.txt
#   zh-launcher-<tag>.zip  ZHLauncher.exe (single-file, self-contained win-x64)
#   zh-symbols-<tag>.pdb   generalszh.pdb (separate asset; never inside the game zip)
# Assumes build.ps1 has produced build\win32\GeneralsMD\Release\generalszh.exe. Does not build, tag or upload.
param(
  [Parameter(Mandatory)][string]$Tag,
  [string]$OutDir = "",
  [string]$Sha = "",
  [switch]$SkipLauncher,
  [switch]$NoDxvk,
  [string]$DxvkVersion = "2.5.3"   # bump deliberately; https://github.com/doitsujin/dxvk/releases
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $OutDir) { $OutDir = Join-Path $repo "out" }
if (-not $Sha) { $Sha = (git -C $repo rev-parse HEAD) }
$game = Join-Path $OutDir "game"
if (Test-Path $game) { Remove-Item $game -Recurse -Force }
New-Item -ItemType Directory -Force $game | Out-Null

$exe = Get-ChildItem (Join-Path $repo "build") -Recurse -Include generalszh.exe -ErrorAction SilentlyContinue |
       Where-Object FullName -match Release | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $exe) { throw "generalszh.exe not found under build\; run scripts\build.ps1 first" }
Write-Host "exe:    $($exe.FullName) ($($exe.LastWriteTime))"
Copy-Item $exe.FullName $game
Get-ChildItem $exe.DirectoryName -Filter *.dll | Copy-Item -Destination $game   # none for the stock preset; future-proof

# Symbols: separate asset. Match a crash dump to it by tag.
$pdb = Join-Path $exe.DirectoryName "generalszh.pdb"
if (Test-Path $pdb) { Copy-Item $pdb (Join-Path $OutDir "zh-symbols-$Tag.pdb") -Force } else { Write-Warning "no generalszh.pdb beside the exe" }

# Add-ons: verified against addons\addons.json (release assets of our repo). Missing = fail: the launcher refuses to start when
# assets.sha256 lists a file the zip lacks.
& (Join-Path $PSScriptRoot "fetch-addons.ps1") -Out $game -Quiet
if ($LASTEXITCODE -ne 0) { throw "add-ons missing or failed verification (see scripts\fetch-addons.ps1)" }

# Manifest + shipped config
$manifest = Join-Path $repo "assets.sha256"
if (Test-Path $manifest) { Copy-Item $manifest $game } else { Write-Warning "assets.sha256 missing; launcher will skip the asset check" }
$config = Join-Path $repo "config"
if (Test-Path $config) { Copy-Item (Join-Path $config "*") $game -Recurse -Force }

# DXVK: reference renderer, pinned, identical on both machines. Game is 32-bit -> x32 DLLs; d3d8.dll = native D3D8 (DXVK >= 2.4).
if (-not $NoDxvk) {
  $cache = Join-Path $env:LOCALAPPDATA "zh-dxvk\dxvk-$DxvkVersion"
  if (-not (Test-Path (Join-Path $cache "x32\d3d8.dll"))) {
    New-Item -ItemType Directory -Force (Split-Path $cache) | Out-Null
    $tgz = Join-Path (Split-Path $cache) "dxvk-$DxvkVersion.tar.gz"
    $url = "https://github.com/doitsujin/dxvk/releases/download/v$DxvkVersion/dxvk-$DxvkVersion.tar.gz"
    Write-Host "dxvk:   downloading $url"
    Invoke-WebRequest $url -OutFile $tgz -UseBasicParsing
    tar -xzf $tgz -C (Split-Path $cache)
    if ($LASTEXITCODE -ne 0) { throw "tar failed extracting $tgz" }
  }
  Copy-Item (Join-Path $cache "x32\d3d8.dll"), (Join-Path $cache "x32\d3d9.dll") $game
  @"
# DXVK config shipped with every release; identical on both machines.
d3d9.samplerAnisotropy = 16
d3d9.forceSwapchainMSAA = 4
d3d9.maxFrameLatency = 1
"@ | Set-Content (Join-Path $game "dxvk.conf")
}

Set-Content (Join-Path $game "BUILD_VERSION.txt") "$Tag $Sha"

# Launcher
if (-not $SkipLauncher) {
  $launcherOut = Join-Path $OutDir "launcher"
  dotnet publish (Join-Path $repo "launcher\ZHLauncher.csproj") -c Release -r win-x64 --nologo -v q `
    -p:PublishSingleFile=true -p:SelfContained=true -p:IncludeNativeLibrariesForSelfExtract=true -o $launcherOut | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "launcher publish failed" }
  $lz = Join-Path $OutDir "zh-launcher-$Tag.zip"; if (Test-Path $lz) { Remove-Item $lz -Force }
  Compress-Archive -Path (Join-Path $launcherOut "ZHLauncher.exe") -DestinationPath $lz
}

$gz = Join-Path $OutDir "zh-game-$Tag.zip"; if (Test-Path $gz) { Remove-Item $gz -Force }
Compress-Archive -Path (Join-Path $game "*") -DestinationPath $gz

Write-Host "packaged:"
Get-ChildItem $OutDir -File | Where-Object Name -match "^zh-.*$([regex]::Escape($Tag))" | ForEach-Object { "  {0,-32} {1,10:N0} KB" -f $_.Name, ($_.Length / 1KB) } | Write-Host
Get-ChildItem $game | ForEach-Object { "    game/{0,-36} {1,10:N0}" -f $_.Name, $_.Length } | Write-Host
