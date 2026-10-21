# Snapshot the local custom-map folder into a versioned map pack the launcher can install on both machines.
#   .\scripts\make-map-pack.ps1 -Version maps-v1                      # -> out\zh-maps-maps-v1.zip + .sha256 (from Documents\...\Maps)
#   .\scripts\make-map-pack.ps1 -Version maps-v1 -Source <folder>     # from another folder
#   .\scripts\make-map-pack.ps1 -Version maps-v1 -Publish             # also: gh release create <Version> (private repo, Phase 2)
# Maps are community content (not EA assets), so a private release asset is fine. The launcher reads "MapPack" from
# launcher.json, compares it with Documents\...\Maps\.zh-mappack, and downloads/extracts zh-maps-<Version>.zip on mismatch.
# The zip is never committed (out\ is gitignored).
param(
  [Parameter(Mandatory)][string]$Version,
  [string]$Source = "$env:USERPROFILE\Documents\Command and Conquer Generals Zero Hour Data\Maps",
  [string]$OutDir = "",
  [switch]$Publish
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $OutDir) { $OutDir = Join-Path $repo "out" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
if (-not (Test-Path $Source)) { throw "map folder not found: $Source" }

$maps = Get-ChildItem $Source -Directory | Where-Object { Get-ChildItem $_.FullName -Filter *.map -File -ErrorAction SilentlyContinue }
if (-not $maps) { throw "no map folders (containing a .map) under $Source" }
$zip = Join-Path $OutDir "zh-maps-$Version.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }

# Stage into a temp dir so the zip contains only map folders plus a version marker, nothing else from Documents.
$stage = Join-Path $env:TEMP ("zh-mappack-" + [guid]::NewGuid())
New-Item -ItemType Directory -Force $stage | Out-Null
foreach ($m in $maps) { Copy-Item $m.FullName (Join-Path $stage $m.Name) -Recurse }
Set-Content (Join-Path $stage ".zh-mappack") $Version
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -CompressionLevel Optimal
Remove-Item $stage -Recurse -Force

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content "$zip.sha256" "$hash  $(Split-Path $zip -Leaf)"
$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "Wrote $zip ($($maps.Count) maps, $size MB) sha256 $($hash.Substring(0,16))"

if ($Publish) {
  $gh = Get-Command gh -ErrorAction SilentlyContinue; if (-not $gh) { $gh = Get-Item "C:\Program Files\GitHub CLI\gh.exe" -ErrorAction SilentlyContinue }
  if (-not $gh) { throw "gh not found" }
  & $gh.Source release create $Version $zip "$zip.sha256" --title "Map pack $Version" --notes "$($maps.Count) custom maps, $size MB. Installed by ZHLauncher when launcher.json MapPack = $Version."
  if ($LASTEXITCODE -ne 0) { throw "gh release create failed (is origin set up? see PLAN.md Phase 2)" }
  Write-Host "Published. Set `"MapPack`": `"$Version`" in launcher.json on both machines." -ForegroundColor Green
}
