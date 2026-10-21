# One-shot environment report for the agent at session start. Prints, never fails.
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
function Try($name, $sb) { try { "$name`: $(& $sb)" } catch { "$name`: MISSING" } }
Try "branch"   { git -C $repo rev-parse --abbrev-ref HEAD }
Try "head"     { git -C $repo log -1 --format="%h %s" }
Try "dirty"    { (git -C $repo status --porcelain | Measure-Object).Count.ToString() + " files" }
Try "upstream" { git -C $repo rev-list --count HEAD..upstream/main 2>$null; "commits behind" }
Try "local"    { (Get-Content "$repo\.zh-local.json" | ConvertFrom-Json).gameDir }
Try "exe"      { (Get-ChildItem "$repo\build" -Recurse -Include generalszh.exe | ? FullName -match Release | select -First 1).LastWriteTime }
Try "pdb"      { (Get-ChildItem "$repo\build" -Recurse -Include generalszh.pdb | select -First 1).FullName }
Try "msvc"     { (Get-ChildItem "C:\Program Files*\Microsoft Visual Studio\2022\*\VC\Auxiliary\Build\vcvars32.bat" | select -First 1).FullName }
Try "cmake"    { (cmake --version | select -First 1) }
Try "dotnet"   { dotnet --version }
Try "vulkan"   { Test-Path "$env:SystemRoot\System32\vulkan-1.dll" }
Try "replays"  { (Get-ChildItem "$repo\tests\replays\*.rep" -ErrorAction SilentlyContinue | Measure-Object).Count }
Try "baselines"{ (Get-ChildItem "$repo\tests\baseline\*.crc" -ErrorAction SilentlyContinue | Measure-Object).Count }
Try "lastrun"  { (Get-ChildItem "$repo\tests\out\run-*" -Directory | sort Name -Desc | select -First 1).Name }
