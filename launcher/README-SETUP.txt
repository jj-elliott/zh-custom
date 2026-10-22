ZERO HOUR CUSTOM BUILD - SETUP
==============================

You need: Command & Conquer Generals Zero Hour installed from Steam (English), 64-bit Windows 10/11,
and a graphics driver with Vulkan (any NVIDIA/AMD/Intel driver from the last few years).

1. Unzip this kit anywhere (your Downloads folder is fine).
2. Double-click ZHLauncher.exe.

That's it. On the first run it:
- finds your Steam copy of Zero Hour and makes a clean copy at C:\Games\ZeroHour-Custom
  (Steam's own files are never touched; the copy takes a couple of minutes),
- puts a "Zero Hour (custom)" shortcut on your desktop - use that from now on,
- downloads the newest build (about 45 MB) and the shared custom maps, checks everything matches, and starts the game.

Every later run checks for updates, updates itself if needed (it may restart once; that is normal), and starts the game.
You never need a new copy of these files.

If it says "Not launching": read the lines above it, they name the exact file that differs. The usual cause is a game
file changed by GenTool/GenPatcher. Fix: in Steam, right-click Zero Hour > Properties > Installed Files > Verify integrity
of game files, then run the desktop shortcut again. If the game crashes, the launcher mentions a crash dump next time;
send the small CrashM*.dmp file plus BUILD_VERSION.txt.

After a laggy or stuttery online game: the launcher window shows "Last network session log: ..." on the next start.
Send that file (logs\net-....log, a few hundred KB at most); it records what the network was doing second by second.
