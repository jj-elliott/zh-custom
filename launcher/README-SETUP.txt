ZERO HOUR CUSTOM BUILD - ONE-TIME SETUP (about 10 minutes)
===========================================================

You need: Command & Conquer Generals Zero Hour installed from Steam (English), 64-bit Windows 10/11,
and a graphics driver with Vulkan (any NVIDIA/AMD/Intel driver from the last few years).

1. Make a copy of the game folder. Steam's own copy stays untouched as a fallback.
   - In Steam: right-click Zero Hour > Manage > Browse local files. That opens the game folder.
   - Copy that whole folder to, for example:  C:\Games\ZeroHour-Custom

2. IMPORTANT: the copy must be plain Steam files. If GenTool or GenPatcher was ever installed:
   - In Steam: right-click Zero Hour > Properties > Installed Files > Verify integrity of game files.
   - Then in your copy delete, if present: d3d8.dll, d3d8.cfg, GenToolUpdater.exe, EdgeScroller.exe,
     CustomContentMaps.big, any file starting with three digits and an underscore (e.g. 340_ControlBarPro...),
     and the folder named "launcher".
   (GenTool crashes this build; GenPatcher changes game rules, which breaks playing together.)

3. Put the two files from this kit into that copy:  ZHLauncher.exe  and  launcher.json

4. Make a desktop shortcut to ZHLauncher.exe. From now on, always start the game from that shortcut.

What the launcher does every time:
- checks GitHub for the newest build and installs it (first time: about 45 MB download)
- checks that your game files match the release exactly (if not, it tells you what differs and stops)
- installs the shared custom maps pack
- starts the game

If it ever says "Not launching" read the lines above it; that is the reason. If the game crashes, the launcher
will mention a crash dump file next time; send the small CrashM*.dmp file plus BUILD_VERSION.txt.
