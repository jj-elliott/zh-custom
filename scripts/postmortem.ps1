# Crash postmortem in one command. Gathers everything about the most recent crash into tests\out\crash-<stamp>\ and
# prints a verdict the agent can read directly: exception code, faulting module + offset, whether that module is
# ours / Windows / third-party, and the symbolized stack.
#   .\scripts\postmortem.ps1                 # newest minidump in the game's user-data CrashDumps folder
#   .\scripts\postmortem.ps1 -Dump x.dmp     # a specific dump
#   .\scripts\postmortem.ps1 -Full           # prefer the newest full-memory dump (CrashF*) over the minidump (CrashM*)
# Upstream's MiniDumper (Core\Libraries\Source\MiniDumper) writes CrashM<game>-<date>-<time>-<githash>-pid<N>.dmp (minimal)
# and CrashF... (full) into %USERPROFILE%\Documents\Command and Conquer Generals Zero Hour Data\CrashDumps\.
# Output: dump copy (minidump only), modules.txt, stack.txt (cdb), summary.txt, ReleaseCrashInfo.txt, newest DebugLogFile*,
# BUILD_VERSION.txt, Options.ini. Last line printed is OUTPUT_DIR=<folder>.
param([string]$Dump = "", [switch]$Full, [switch]$NoSymbolize)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$userData = Join-Path $env:USERPROFILE "Documents\Command and Conquer Generals Zero Hour Data"
$local = if (Test-Path "$repo\.zh-local.json") { Get-Content "$repo\.zh-local.json" | ConvertFrom-Json } else { $null }
$game = if ($local) { $local.gameDir } else { "" }

if (-not $Dump) {
  $pattern = if ($Full) { "CrashF*.dmp" } else { "CrashM*.dmp" }
  $cands = @(Get-ChildItem "$userData\CrashDumps\$pattern" -ErrorAction SilentlyContinue)
  if ($game) { $cands += @(Get-ChildItem "$game\crashdumps\*.dmp" -ErrorAction SilentlyContinue) }
  $d = $cands | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (-not $d) { throw "No crash dumps found under $userData\CrashDumps or $game\crashdumps" }
  $Dump = $d.FullName
}
$Dump = (Resolve-Path $Dump).Path
$dumpItem = Get-Item $Dump
$stamp = $dumpItem.LastWriteTime.ToString("yyyyMMdd-HHmmss")
$out = Join-Path $repo "tests\out\crash-$stamp"
New-Item -ItemType Directory -Force $out | Out-Null
$summary = New-Object System.Collections.Generic.List[string]
function Say($s) { $script:summary.Add($s); Write-Host $s }

Say "dump: $Dump ($([math]::Round($dumpItem.Length/1MB,1)) MB, written $($dumpItem.LastWriteTime))"
if ($dumpItem.Name -match '-([0-9a-f]{7,})-pid(\d+)\.dmp$') { Say "build (from dump name): $($Matches[1])   pid: $($Matches[2])" }
if ($dumpItem.Length -lt 64MB) { Copy-Item $Dump $out -Force }

# ---- Minidump parsing (no debugger needed): module list + exception record --------------------------------------
$b = [IO.File]::ReadAllBytes($Dump)
function U32($o) { [BitConverter]::ToUInt32($b, $o) }
function U64($o) { [BitConverter]::ToUInt64($b, $o) }
if ([Text.Encoding]::ASCII.GetString($b, 0, 4) -ne "MDMP") { throw "$Dump is not a minidump" }
$nStreams = U32 8; $streamRva = U32 12
$modules = @(); $exc = $null
for ($i = 0; $i -lt $nStreams; $i++) {
  $e = $streamRva + 12 * $i
  $type = U32 $e; $rva = U32 ($e + 8)
  if ($type -eq 4) {            # ModuleListStream
    $count = U32 $rva; $off = $rva + 4
    for ($m = 0; $m -lt $count; $m++) {
      $nameRva = U32 ($off + 20); $len = U32 $nameRva
      $modules += [pscustomobject]@{ Base = (U64 $off); Size = (U32 ($off + 8)); Name = [Text.Encoding]::Unicode.GetString($b, $nameRva + 4, $len) }
      $off += 108
    }
  } elseif ($type -eq 6) {      # ExceptionStream: ThreadId, pad, then MINIDUMP_EXCEPTION (Code, Flags, Record, Address, ...)
    $exc = [pscustomobject]@{ Thread = (U32 $rva); Code = (U32 ($rva + 8)); Address = (U64 ($rva + 24)) }
  }
}
$modules = $modules | Sort-Object Base
$modules | ForEach-Object { "{0:X8}-{1:X8} {2}" -f $_.Base, ($_.Base + $_.Size), $_.Name } | Set-Content (Join-Path $out "modules.txt")

function Classify($path) {
  if (-not $path) { return "unknown" }
  $n = [IO.Path]::GetFileName($path)
  if ($n -ieq "generalszh.exe") { return "OURS (engine)" }
  if ($path -like "$env:SystemRoot\*") { return "windows" }
  $vanilla = "BINKW32.DLL","mss32.dll","P2XDLL.DLL","DebugWindow.dll","ParticleEditor.dll","patchw32.dll","steam_api.dll"
  if ($game -and $path -like "$game\MSS\*") { return "game folder (vanilla Miles Sound System plugin)" }
  if ($game -and $path -like "$game\*") { if ($vanilla -icontains $n) { return "game folder (vanilla DLL)" } else { return "game folder, THIRD-PARTY (not vanilla: wrapper/hook/overlay?)" } }
  return "other"
}
if ($exc) {
  $hit = $modules | Where-Object { $_.Base -le $exc.Address -and $exc.Address -lt ($_.Base + $_.Size) } | Select-Object -First 1
  $codeName = switch ($exc.Code) { 0xC0000005 { "ACCESS_VIOLATION" } 0xC0000094 { "INT_DIVIDE_BY_ZERO" } 0xC00000FD { "STACK_OVERFLOW" } 0x80000003 { "BREAKPOINT" } 0xE06D7363 { "C++ exception" } 0xC0000409 { "STACK_BUFFER_OVERRUN / fail-fast" } default { "" } }
  Say ("exception: 0x{0:X8} {1}  thread {2}  at 0x{3:X8}" -f $exc.Code, $codeName, $exc.Thread, $exc.Address)
  if ($hit) { Say ("faulting module: {0} +0x{1:X}   [{2}]" -f $hit.Name, ($exc.Address - $hit.Base), (Classify $hit.Name)) }
  else      { Say "faulting module: (address not inside any loaded module: JIT/hook trampoline/freed code?)" }
} else { Say "exception: none recorded in dump (manual/hang dump?)" }

# Third-party binaries loaded from the game folder are the usual suspects (GenTool, overlays, wrappers).
if ($game) {
  $tp = $modules | Where-Object { (Classify $_.Name) -like "*THIRD-PARTY*" } | ForEach-Object { [IO.Path]::GetFileName($_.Name) }
  if ($tp) { Say "third-party modules loaded from game folder: $($tp -join ', ')" } else { Say "third-party modules loaded from game folder: none" }
}

# ---- Companion files ---------------------------------------------------------------------------------------------
foreach ($f in "ReleaseCrashInfo.txt", "Options.ini") { $p = Join-Path $userData $f; if (Test-Path $p) { Copy-Item $p $out -Force } }
$dbg = Get-ChildItem "$userData\DebugLogFile*.txt", "$game\DebugLogFile*.txt" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($dbg) { Copy-Item $dbg.FullName $out -Force; Say "debug log: $($dbg.Name)" }
if ($game -and (Test-Path "$game\BUILD_VERSION.txt")) { Copy-Item "$game\BUILD_VERSION.txt" $out -Force; Say "installed build: $(Get-Content "$game\BUILD_VERSION.txt")" }
$rci = Join-Path $userData "ReleaseCrashInfo.txt"
if (Test-Path $rci) { $reason = (Select-String -Path $rci -Pattern '^; Reason|^Release Crash at' | Select-Object -First 2 | ForEach-Object { $_.Line }) -join " | "; if ($reason) { Say "ReleaseCrashInfo: $reason" } }

# ---- Symbolized stack --------------------------------------------------------------------------------------------
if (-not $NoSymbolize) {
  try {
    $stack = & (Join-Path $PSScriptRoot "symbolize.ps1") -Dump $Dump 2>&1 | Out-String -Width 220
    Set-Content (Join-Path $out "stack.txt") $stack
    $lines = $stack -split "`r?`n"
    $start = [array]::IndexOf($lines, ($lines | Where-Object { $_ -match '^STACK_TEXT' } | Select-Object -First 1))
    if ($start -ge 0) {
      Say "stack (top frames):"
      $lines[($start + 1)..([math]::Min($start + 16, $lines.Count - 1))] | Where-Object { $_.Trim() } | ForEach-Object { Say "  $_" }
    } else { Say "stack: cdb produced no STACK_TEXT (see stack.txt)" }
    $bucket = $lines | Where-Object { $_ -match '^FAILURE_BUCKET_ID' } | Select-Object -First 1
    if ($bucket) { Say $bucket.Trim() }
  } catch { Say "symbolize failed: $($_.Exception.Message)" }
}

$summary | Set-Content (Join-Path $out "summary.txt")
Write-Host "OUTPUT_DIR=$out"
