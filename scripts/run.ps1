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
  # Capture the game's own window with PrintWindow (PW_RENDERFULLCONTENT) so the shot is the game even when another
  # window is in front of it; fall back to the primary screen when the process has no window yet.
  # DPI awareness first: on a scaled desktop (4K at 150%) the unaware bounds crop the bottom/right of the image.
  Add-Type -Namespace Native -Name Win -MemberDefinition @'
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
'@
  [Native.Win]::SetProcessDPIAware() | Out-Null
  $hwnd = [IntPtr]::Zero
  for ($i = 0; $i -lt 20 -and $hwnd -eq [IntPtr]::Zero; $i++) { $p.Refresh(); $hwnd = $p.MainWindowHandle; if ($hwnd -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 250 } }
  $rc = New-Object Native.Win+RECT
  if ($hwnd -ne [IntPtr]::Zero -and [Native.Win]::GetClientRect($hwnd, [ref]$rc) -and ($rc.Right - $rc.Left) -gt 0) {
    $bmp = New-Object System.Drawing.Bitmap ($rc.Right - $rc.Left), ($rc.Bottom - $rc.Top)
    $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
    [Native.Win]::PrintWindow($hwnd, $hdc, 3) | Out-Null   # 1 = PW_CLIENTONLY, 2 = PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save("$out\screenshot.png"); $bmp.Dispose()
    Write-Host "screenshot: game window $($rc.Right - $rc.Left)x$($rc.Bottom - $rc.Top)"
  } else {
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap $b.Width,$b.Height
    [System.Drawing.Graphics]::FromImage($bmp).CopyFromScreen($b.Location,[System.Drawing.Point]::Empty,$b.Size)
    $bmp.Save("$out\screenshot.png"); $bmp.Dispose()
    Write-Host "screenshot: no game window found, captured the primary screen"
  }
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
