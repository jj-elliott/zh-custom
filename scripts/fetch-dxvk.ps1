# Put the pinned DXVK (reference renderer) into a game folder: x32 d3d8.dll + d3d9.dll and our dxvk.conf.
#   .\scripts\fetch-dxvk.ps1 -Out <game folder> [-Version 2.5.3]
# Used by package.ps1 (release zip) and stage-install.ps1 (dev copy) so both carry the identical renderer. Cached under
# %LOCALAPPDATA%\zh-dxvk. The game is 32-bit; d3d8.dll is DXVK's native D3D8 (>= 2.4), no d3d8to9 shim.
param([Parameter(Mandatory)][string]$Out, [string]$Version = "2.5.3")   # bump deliberately; https://github.com/doitsujin/dxvk/releases
$ErrorActionPreference = "Stop"
$cache = Join-Path $env:LOCALAPPDATA "zh-dxvk\dxvk-$Version"
if (-not (Test-Path (Join-Path $cache "x32\d3d8.dll"))) {
  New-Item -ItemType Directory -Force (Split-Path $cache) | Out-Null
  $tgz = Join-Path (Split-Path $cache) "dxvk-$Version.tar.gz"
  $url = "https://github.com/doitsujin/dxvk/releases/download/v$Version/dxvk-$Version.tar.gz"
  Write-Host "dxvk:   downloading $url"
  Invoke-WebRequest $url -OutFile $tgz -UseBasicParsing
  tar -xzf $tgz -C (Split-Path $cache)
  if ($LASTEXITCODE -ne 0) { throw "tar failed extracting $tgz" }
}
New-Item -ItemType Directory -Force $Out | Out-Null
Copy-Item (Join-Path $cache "x32\d3d8.dll"), (Join-Path $cache "x32\d3d9.dll") $Out -Force
@"
# DXVK config shipped with every release; identical on both machines.
d3d9.samplerAnisotropy = 16
d3d9.forceSwapchainMSAA = 4
d3d9.maxFrameLatency = 1
# Vsync: the engine renders uncapped (logic stays 30 Hz); without this the swapchain presents immediately and tears.
d3d9.presentInterval = 1
"@ | Set-Content (Join-Path $Out "dxvk.conf")
Write-Host "dxvk:   $Version -> $Out"
