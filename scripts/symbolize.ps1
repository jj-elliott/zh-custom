# Turn a crash dump into a readable stack trace, for the agent to read directly.
#   .\scripts\symbolize.ps1 path\to\crash.dmp
# Uses cdb from the Windows SDK Debugging Tools; installs them via winget if missing.
param([Parameter(Mandatory)][string]$Dump)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
# cdb comes from the SDK "Debugging Tools" feature or from the Store WinDbg package (winget Microsoft.WinDbg), whose
# app-execution alias is cdbX86.exe. The game is a 32-bit process, so prefer the x86 debugger.
function Find-Cdb {
  Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe",
                "$env:LOCALAPPDATA\Microsoft\WindowsApps\cdbX86.exe",
                "$env:LOCALAPPDATA\Microsoft\WindowsApps\cdb.exe" -ErrorAction SilentlyContinue | select -First 1
}
$cdb = Find-Cdb
if (-not $cdb) {
  winget install --id Microsoft.WinDbg -e --accept-source-agreements --accept-package-agreements | Out-Null
  $cdb = Find-Cdb
  if (-not $cdb) { throw "cdb.exe not found; install 'Debugging Tools for Windows' from the Windows SDK installer" }
}
$pdbDirs = (Get-ChildItem "$repo\build" -Recurse -Include generalszh.pdb -ErrorAction SilentlyContinue | % DirectoryName | select -Unique) -join ";"
$symPath = "srv*$env:TEMP\symcache*https://msdl.microsoft.com/download/symbols;$pdbDirs"
$ErrorActionPreference = "Continue"   # PS 5.1 + Stop would turn cdb's redirected stderr into a terminating error
& $cdb.FullName -z $Dump -y $symPath -c "!analyze -v; .ecxr; kv 40; q" 2>&1 | Out-String
