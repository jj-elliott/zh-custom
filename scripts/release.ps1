# Attended release from this machine (the primary path; release.yml is the manual fallback).
#   .\scripts\release.ps1 -Tag v0.1.1              # gate (clean tree, on main, verify.ps1) -> package -> tag -> gh release
#   .\scripts\release.ps1 -Tag v0.1.1 -SkipVerify  # skip the replay check (not for sim changes)
#   .\scripts\release.ps1 -Tag v0.1.1 -DryRun      # everything except tag/push/upload
# Rules (CLAUDE.md): tag only from main, main releasable only. The tag goes on HEAD.
param(
  [Parameter(Mandatory)][string]$Tag,
  [string]$Notes = "",
  [switch]$SkipVerify,
  [switch]$SkipBuild,
  [switch]$DryRun,
  [switch]$Prerelease
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repo
$env:Path = [Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [Environment]::GetEnvironmentVariable("Path","User")
$gh = Get-Command gh -ErrorAction SilentlyContinue; if (-not $gh) { $gh = Get-Item "C:\Program Files\GitHub CLI\gh.exe" }

if ($Tag -notmatch '^v\d+\.\d+\.\d+') { throw "tag must look like v1.2.3 (got '$Tag')" }
if ((git branch --show-current) -ne "main") { throw "release only from main (on '$(git branch --show-current)')" }
if (git status --porcelain) { throw "working tree not clean; commit or stash first" }
if (git tag -l $Tag) { throw "tag $Tag already exists locally" }
git fetch -q origin
if ((git rev-parse HEAD) -ne (git rev-parse origin/main)) { throw "main is not in sync with origin/main; push or pull first" }
$sha = git rev-parse HEAD

# Gate: build + every baseline replay must reproduce.
if ($SkipVerify) { if (-not $SkipBuild) { & "$PSScriptRoot\build.ps1"; if ($LASTEXITCODE -ne 0) { throw "build failed" } } }
else {
  $vargs = @(); if ($SkipBuild) { $vargs += "-SkipBuild" }
  & "$PSScriptRoot\verify.ps1" @vargs
  if ($LASTEXITCODE -ne 0) { throw "verify.ps1 reported $LASTEXITCODE failing replay(s); not releasing" }
}

& "$PSScriptRoot\package.ps1" -Tag $Tag -Sha $sha
$out = Join-Path $repo "out"
$assets = @("zh-game-$Tag.zip", "zh-launcher-$Tag.zip", "zh-symbols-$Tag.pdb") | ForEach-Object { Join-Path $out $_ } | Where-Object { Test-Path $_ }

if ($DryRun) { Write-Host "dry run: would tag $Tag at $sha and upload: $($assets -join ', ')" -ForegroundColor Yellow; exit 0 }

if (-not $Notes) { $Notes = "Built on $env:COMPUTERNAME from $sha.`n`n" + ((git log --oneline -15 --no-decorate) -join "`n") }
git tag -a $Tag -m "$Tag"
$ErrorActionPreference = "Continue"   # git/gh write progress to stderr
git push -q origin $Tag
if ($LASTEXITCODE -ne 0) { throw "tag push failed" }
$pre = @(); if ($Prerelease) { $pre = @("--prerelease") }
& $gh.Source release create $Tag @assets --title $Tag --notes $Notes --verify-tag @pre
if ($LASTEXITCODE -ne 0) { throw "gh release create failed" }
$ErrorActionPreference = "Stop"
Write-Host "released ${Tag}: $(& $gh.Source release view $Tag --json url --jq .url)" -ForegroundColor Green
Write-Host "Both launchers will pick it up on next start (releases/latest)."
