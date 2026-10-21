# Launch the dev build and capture everything an agent needs, without a human in the loop.
#   .\scripts\run.ps1                          # launch, wait for exit, print tail of logs
#   .\scripts\run.ps1 -Args "-win -xres 1280 -yres 720 -noshellmap"
#   .\scripts\run.ps1 -TimeoutSec 60           # kill after N seconds (smoke test: "does it reach the menu?")
#   .\scripts\run.ps1 -Screenshot 20           # screenshot the game window after N seconds (needs windowed mode)
# Output: tests\out\run-<timestamp>\ with stdout.txt, exitcode.txt, any new crashdump, screenshot.png, and a copy of
# the game's own log files written during the run. The folder path is printed last so the agent can read it.
# NOTE: the parameter is $GameArgs, not $Args: $Args is a PowerShell automatic variable and a parameter of that name
# binds unreliably (Start-Process saw it as empty). The -Args alias keeps the documented command line working.
param([Alias("Args")][string]$GameArgs = "-win", [int]$TimeoutSec = 0, [int]$Screenshot = 0, [switch]$NoCopyExe)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$local = Get-Content "$repo\.zh-local.json" | ConvertFrom-Json
$game = $local.gameDir
$out = "$repo\tests\out\run-$(Get-Date -Format yyyyMMdd-HHmmss)"
New-Item -ItemType Directory -Force $out | Out-Null

if (-not $NoCopyExe) {
  $exe = Get-ChildItem "$repo\build" -Recurse -Include generalszh.exe | ? FullName -match Release | select -First 1
  if ($exe) { Copy-Item $exe.FullName "$game\generalszh.exe" -Force }
}
$before = Get-ChildItem "$game\crashdumps\*.dmp","$env:USERPROFILE\Documents\Command and Conquer Generals Zero Hour Data\CrashDumps\*.dmp" -ErrorAction SilentlyContinue | % FullName
$startTime = Get-Date

$p = Start-Process -FilePath "$game\generalszh.exe" -ArgumentList $GameArgs -WorkingDirectory $game -PassThru `
       -RedirectStandardOutput "$out\stdout.txt" -RedirectStandardError "$out\stderr.txt"

if ($Screenshot -gt 0) {
  Start-Sleep $Screenshot
  Add-Type -AssemblyName System.Windows.Forms,System.Drawing
  # Without DPI awareness, a scaled desktop (e.g. 4K at 150%) reports scaled bounds and CopyFromScreen grabs only the top-left
  # part of the physical screen, cropping the bottom/right of the game window.
  Add-Type -Namespace Native -Name Dpi -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();'
  [Native.Dpi]::SetProcessDPIAware() | Out-Null
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $b.Width,$b.Height
  [System.Drawing.Graphics]::FromImage($bmp).CopyFromScreen($b.Location,[System.Drawing.Point]::Empty,$b.Size)
  $bmp.Save("$out\screenshot.png"); $bmp.Dispose()
}

if ($TimeoutSec -gt 0) {
  if (-not $p.WaitForExit($TimeoutSec * 1000)) { $p.Kill(); Set-Content "$out\exitcode.txt" "KILLED after ${TimeoutSec}s (alive = good for a smoke test)" }
  else { Set-Content "$out\exitcode.txt" $p.ExitCode }
} else { $p.WaitForExit(); Set-Content "$out\exitcode.txt" $p.ExitCode }

# Harvest: new crash dumps, and any log the game wrote during the run.
# Upstream's MiniDumper writes to the user-data folder (Documents), not the game dir; minidumps (CrashM*) are small, full dumps (CrashF*) ~700 MB.
$userData = "$env:USERPROFILE\Documents\Command and Conquer Generals Zero Hour Data"
$newDumps = Get-ChildItem "$game\crashdumps\*.dmp","$userData\CrashDumps\CrashM*.dmp" -ErrorAction SilentlyContinue | ? { $_.FullName -notin $before -and $_.LastWriteTime -gt $startTime }
$newDumps | Copy-Item -Destination $out
Get-ChildItem $game,"$game\logs",$userData -Include *.log,*.txt -Recurse -ErrorAction SilentlyContinue |
  ? { $_.LastWriteTime -gt $startTime -and $_.Name -ne "BUILD_VERSION.txt" } | Copy-Item -Destination $out -ErrorAction SilentlyContinue

Write-Host "exit: $(Get-Content "$out\exitcode.txt")"
Get-ChildItem $out | % { Write-Host "  $($_.Name) ($($_.Length) B)" }
if ($newDumps) {
  # It crashed: run the postmortem right away so the verdict is in this output.
  Write-Host "CRASHED: $($newDumps.Count) new dump(s); running postmortem" -ForegroundColor Red
  & "$PSScriptRoot\postmortem.ps1" -Dump ($newDumps | sort LastWriteTime -Desc | select -First 1).FullName
}
Write-Host "OUTPUT_DIR=$out"
