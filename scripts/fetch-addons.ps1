# Populate the add-on cache (addons\addons.json → cacheDir) and verify every file against its pinned size + sha256 prefix.
#   .\scripts\fetch-addons.ps1                          # download enabled add-ons whose "url" is set; verify what is cached
#   .\scripts\fetch-addons.ps1 -ImportFrom "<folder>"   # seed the cache from a folder that already has the .big files (e.g. a GenPatcher install)
#   .\scripts\fetch-addons.ps1 -Out out\game -All       # (CI) copy every enabled, verified add-on into a staging folder
# Exit code = number of enabled files that are missing or fail verification. Nothing is ever committed; the cache lives outside the repo.
param([string]$ImportFrom = "", [string]$Out = "", [switch]$All, [switch]$Quiet)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cfg = Get-Content (Join-Path $repo "addons\addons.json") -Raw | ConvertFrom-Json
$cache = $cfg.cacheDir
if (-not $cache) { $cache = Join-Path $env:LOCALAPPDATA "zh-addons" }
New-Item -ItemType Directory -Force $cache | Out-Null

function Verify($path, $spec) {
  if (-not (Test-Path $path)) { return "missing" }
  $f = Get-Item $path
  if ($f.Length -ne $spec.size) { return "size $($f.Length) != $($spec.size)" }
  $h = (Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant()
  if (-not $h.StartsWith($spec.sha256.ToLowerInvariant())) { return "sha256 $($h.Substring(0,16)) != $($spec.sha256)" }
  return "ok"
}

$bad = 0
foreach ($a in $cfg.addons) {
  if (-not $a.enabled -and -not $All) { continue }
  if (-not $Quiet) { Write-Host "== $($a.name) [$($a.licence)]" }
  foreach ($f in $a.files) {
    $dst = Join-Path $cache $f.name
    if ($ImportFrom) {
      $src = Join-Path $ImportFrom $f.name
      if (Test-Path $src) { Copy-Item $src $dst -Force }
    } elseif ((Verify $dst $f) -ne "ok" -and $a.release) {
      # Hosted as an asset of a release in OUR repo (private is fine: gh authenticates; CI gets GH_TOKEN). Set "release" per add-on.
      $gh = Get-Command gh -ErrorAction SilentlyContinue; if (-not $gh) { $gh = Get-Item "C:\Program Files\GitHub CLI\gh.exe" -ErrorAction SilentlyContinue }
      $repoArg = @(); if ($cfg.repo) { $repoArg = @("-R", $cfg.repo) }
      if (-not $Quiet) { Write-Host "   gh release download $($a.release) $($f.name)" }
      if ($gh) { & $gh.Source release download $a.release @repoArg -p $f.name -D $cache --clobber 2>&1 | ForEach-Object { if (-not $Quiet) { "   $_" } } }
      else { Write-Warning "   gh not found; cannot download release asset" }
    } elseif ((Verify $dst $f) -ne "ok" -and $a.url) {
      $u = "$($a.url.TrimEnd('/'))/$($f.name)"
      if (-not $Quiet) { Write-Host "   downloading $u" }
      try { Invoke-WebRequest $u -OutFile $dst -UseBasicParsing } catch { Write-Warning "   download failed: $($_.Exception.Message)" }
    }
    $v = Verify $dst $f
    if ($v -ne "ok") { $bad++ }
    if (-not $Quiet -or $v -ne "ok") { Write-Host ("   {0,-6} {1,-36} {2}" -f $(if ($v -eq "ok") { "ok" } else { "FAIL" }), $f.name, $(if ($v -eq "ok") { "" } else { $v })) }
    if ($Out -and $v -eq "ok" -and $a.enabled) { New-Item -ItemType Directory -Force $Out | Out-Null; Copy-Item $dst (Join-Path $Out $f.name) -Force }
  }
}
if ($bad) { Write-Warning "$bad add-on file(s) missing or failed verification (cache: $cache). Seed with -ImportFrom <folder> or pin a url in addons\addons.json." }
elseif (-not $Quiet) { Write-Host "all enabled add-ons verified in $cache" -ForegroundColor Green }
exit $bad
