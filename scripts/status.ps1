# One-shot environment report for the agent at session start. Prints, never fails.
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
function Probe($name, $sb) { try { "$name`: $(& $sb)" } catch { "$name`: MISSING" } }
Probe "branch"   { git -C $repo rev-parse --abbrev-ref HEAD }
Probe "head"     { git -C $repo log -1 --format="%h %s" }
Probe "dirty"    { (git -C $repo status --porcelain | Measure-Object).Count.ToString() + " files" }
Probe "upstream" { git -C $repo rev-list --count HEAD..upstream/main 2>$null; "commits behind" }
Probe "local"    { (Get-Content "$repo\.zh-local.json" | ConvertFrom-Json).gameDir }
Probe "exe"      { (Get-ChildItem "$repo\build" -Recurse -Include generalszh.exe | ? FullName -match Release | select -First 1).LastWriteTime }
Probe "pdb"      { (Get-ChildItem "$repo\build" -Recurse -Include generalszh.pdb | select -First 1).FullName }
Probe "msvc"     { (Get-ChildItem "C:\Program Files*\Microsoft Visual Studio\2022\*\VC\Auxiliary\Build\vcvars32.bat" | select -First 1).FullName }
Probe "cmake"    { $c = Get-Command cmake -ErrorAction SilentlyContinue; if (-not $c) { $c = Get-Item "C:\Program Files\CMake\bin\cmake.exe" }; (& $c.Source --version | select -First 1) + " ($($c.Source))" }
Probe "dotnet"   { dotnet --version }
Probe "vulkan"   { Test-Path "$env:SystemRoot\System32\vulkan-1.dll" }
Probe "replays"  { (Get-ChildItem "$repo\tests\replays\*.rep" -ErrorAction SilentlyContinue | Measure-Object).Count }
Probe "baselines"{ (Get-ChildItem "$repo\tests\baseline\*.crc" -ErrorAction SilentlyContinue | Measure-Object).Count }
Probe "lastrun"  { (Get-ChildItem "$repo\tests\out\run-*" -Directory | sort Name -Desc | select -First 1).Name }
