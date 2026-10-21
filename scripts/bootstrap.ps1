<#
.SYNOPSIS
  One-click: turn THIS folder into the Zero Hour working repo, build it, stage a playable dev install,
  and leave it ready for a Claude Code session.

.DESCRIPTION
  Run from an elevated PowerShell inside the unzipped zh-pipeline folder:
      .\scripts\bootstrap.ps1
  Afterwards, `cd` here and run `claude`. CLAUDE.md and docs\ are already in place.

  Steps (all idempotent, re-run freely):
  1. Toolchain via winget: VS 2022 Build Tools (C++ + MFC), CMake, Ninja, Git, GitHub CLI, .NET SDK.
  2. git init this folder, add the upstream fork as remote `upstream`, pull its history onto `main`.
     Our overlay (CLAUDE.md, docs/, scripts/, launcher/, .github/) sits on top of upstream's tree.
  3. Build Zero Hour Release/Win32 in-place (build\ is gitignored).
  4. Stage $DevRoot\ZeroHour-Dev: a COPY of the Steam folder with the fresh exe. Steam copy stays pristine.
     The path is written to .zh-local.json (gitignored) so scripts and the agent know where the game is.
  5. -CreateRepo: private GitHub repo as `origin`, push main. Tag v0.1.0 to trigger the first release.

.NOTES
  Steam's Zero Hour folder on a default install is usually one of:
    C:\Program Files (x86)\Steam\steamapps\common\Command and Conquer Generals - Zero Hour\Command and Conquer Generals Zero Hour
    C:\Program Files (x86)\Steam\steamapps\common\Command & Conquer Generals - Zero Hour
#>
[CmdletBinding()]
param(
  [string]$UpstreamUrl  = "https://github.com/TheSuperHackers/GeneralsGameCode.git",
  [string]$UpstreamBranch = "main",
  [string]$DevRoot      = "C:\Games\ZH",
  [string]$SteamZHPath  = "",
  [switch]$CreateRepo,
  [string]$RepoName     = "zh-custom",
  [switch]$SkipInstall,
  [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
function Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repo
Write-Host "Working repo: $repo"

# ---------- 1. Toolchain ----------
if (-not $SkipInstall) {
  Step "Installing toolchain via winget"
  $vsOverride = '--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools ' +
                '--add Microsoft.VisualStudio.Component.VC.ATLMFC --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ' +
                '--add Microsoft.VisualStudio.Component.Windows11SDK.22621 --includeRecommended'
  winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-source-agreements --accept-package-agreements --override "$vsOverride"
  foreach ($id in "Kitware.CMake","Ninja-build.Ninja","Git.Git","GitHub.cli","Microsoft.DotNet.SDK.8") {
    winget install --id $id -e --accept-source-agreements --accept-package-agreements
  }
  $env:Path = [Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [Environment]::GetEnvironmentVariable("Path","User")
}

# ---------- 2. Make this folder the repo ----------
Step "Initialising repo in place with upstream $UpstreamUrl"
# NOTE: under $ErrorActionPreference = "Stop", PowerShell 5.1 turns any *redirected* stderr from a native command
# (2>$null, 2>&1) into a terminating error. git writes progress and "Switched to branch" to stderr, so git calls in
# this block must not redirect stderr; failures are checked via $LASTEXITCODE instead.
if (-not (Test-Path ".git")) { git init -q -b main; if ($LASTEXITCODE -ne 0) { throw "git init failed" } }
git rev-parse -q --verify HEAD | Out-Null
$hasCommits = ($LASTEXITCODE -eq 0)
if (-not $hasCommits) {
  # Fresh repo, or a previous run died mid-init (idempotent: safe to re-run until the first commit lands).
  if (-not (git remote | Select-String -Quiet "^upstream$")) { git remote add upstream $UpstreamUrl }
  git fetch upstream $UpstreamBranch; if ($LASTEXITCODE -ne 0) { throw "git fetch upstream/$UpstreamBranch failed" }
  # Park our overlay, bring in upstream's tree, then lay the overlay back on top.
  $overlay = "CLAUDE.md","docs","scripts","launcher",".github","config","assets.sha256"
  $park = Join-Path $env:TEMP ("zh-overlay-" + [guid]::NewGuid())
  New-Item -ItemType Directory -Force $park | Out-Null
  foreach ($o in $overlay) { if (Test-Path $o) { Move-Item $o (Join-Path $park $o) -Force } }
  # On an unborn 'main', reset --hard makes main point at upstream and checks out its tree.
  git reset -q --hard "upstream/$UpstreamBranch"; if ($LASTEXITCODE -ne 0) { throw "git reset --hard upstream/$UpstreamBranch failed (overlay parked at $park)" }
  # Merge the overlay back. Upstream already has scripts/ and .github/, and Copy-Item of a directory onto an existing
  # directory nests it (scripts\scripts\), so copy directory *contents* into the destination instead.
  foreach ($o in Get-ChildItem $park -Force) {
    $dst = Join-Path $repo $o.Name
    if ($o.PSIsContainer) {
      New-Item -ItemType Directory -Force $dst | Out-Null
      Copy-Item (Join-Path $o.FullName "*") $dst -Recurse -Force
    } else {
      Copy-Item $o.FullName $dst -Force
    }
  }
  Remove-Item $park -Recurse -Force
  # Our local-only bits. Upstream's .gitignore ignores /CLAUDE.md and may lack a trailing newline, so write a
  # self-contained block (leading newline) and un-ignore our briefing file.
  if (-not (Select-String -Path .gitignore -Pattern "zh-pipeline overlay" -Quiet -ErrorAction SilentlyContinue)) {
    Add-Content .gitignore "`n## zh-pipeline overlay (local-only paths, and un-ignore our agent briefing)`n!/CLAUDE.md`n.zh-local.json`nlogs/`ncrashdumps/`nout/`nlauncher/bin/`nlauncher/obj/`ntests/out/"
  }
  git add -A
  git commit -q -m "Overlay: pipeline, launcher, docs, CLAUDE.md on top of upstream $UpstreamBranch"
  Write-Host "Repo initialised. Upstream history is on 'upstream/$UpstreamBranch'; merge with: git fetch upstream && git merge upstream/$UpstreamBranch"
} else {
  Write-Host "Repo already initialised (HEAD = $(git rev-parse --short HEAD)); skipping init."
}

# ---------- 3. Build ----------
$exe = $null
if (-not $SkipBuild) {
  Step "Building Zero Hour (Release, Win32)"
  & (Join-Path $PSScriptRoot "build.ps1")
  if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}
$exe = Get-ChildItem -Path (Join-Path $repo "build") -Recurse -Include "generalszh.exe" -ErrorAction SilentlyContinue |
       Where-Object { $_.FullName -match "Release" } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $exe) { throw "generalszh.exe not found under build\ (run without -SkipBuild)" }
Write-Host "Built: $($exe.FullName)"

# ---------- 4. Stage a dev install ----------
Step "Staging dev install"
if (-not $SteamZHPath) {
  $candidates = @(
    "C:\Program Files (x86)\Steam\steamapps\common\Command and Conquer Generals - Zero Hour\Command and Conquer Generals Zero Hour",
    "C:\Program Files (x86)\Steam\steamapps\common\Command & Conquer Generals - Zero Hour",
    "C:\Program Files (x86)\Steam\steamapps\common\Command and Conquer Generals - Zero Hour"
  )
  $SteamZHPath = $candidates | Where-Object { Test-Path (Join-Path $_ "generals.exe") } | Select-Object -First 1
  if (-not $SteamZHPath) { throw "Could not find the Steam Zero Hour folder. Re-run with -SteamZHPath '<folder containing generals.exe>'" }
}
$dev = Join-Path $DevRoot "ZeroHour-Dev"
# stage-install.ps1 mirrors the Steam folder minus third-party add-ons (GenTool, GenPatcher), drops in the exe,
# writes .zh-local.json and regenerates assets.sha256. Re-run it any time to get back to a known state.
& (Join-Path $PSScriptRoot "stage-install.ps1") -SteamZHPath $SteamZHPath -DevDir $dev
if (git status --porcelain -- assets.sha256) {
  git add assets.sha256; git commit -q -m "Update asset manifest from local install"
}
Write-Host "Dev install ready: $dev\$($exe.Name)" -ForegroundColor Green

# ---------- 5. GitHub repo ----------
if ($CreateRepo) {
  Step "Creating private GitHub repo '$RepoName'"
  & { $ErrorActionPreference = "Continue"; gh auth status 2>$null | Out-Null }   # gh writes status to stderr
  if ($LASTEXITCODE -ne 0) { gh auth login }
  # git/gh print push progress on stderr; under Stop that would abort after a successful push, so judge by exit code.
  $ErrorActionPreference = "Continue"
  if (git remote | Select-String -Quiet "^origin$") { Write-Host "origin already exists; pushing"; git push -u origin main }
  else { gh repo create $RepoName --private --source . --remote origin --push }
  if ($LASTEXITCODE -ne 0) { throw "repo create/push failed (exit $LASTEXITCODE)" }
  git branch -u origin/main main 2>&1 | Out-Null
  $ErrorActionPreference = "Stop"
  Write-Host "Tag a release with:  git tag v0.1.0 && git push origin v0.1.0" -ForegroundColor Green
}

Write-Host @"

Done.
  Play test:   $dev\$($exe.Name)
  Next:        cd "$repo" ; claude
               (CLAUDE.md tells the agent what to do first; see docs\AGENT-TODO.md)
"@ -ForegroundColor Green
