# Feature parity with "the GenTool version" (investigated 2026-10-21)

Goal: keep everything that was good about the setup played on 2026-10-20 and build from there. This records what that setup
actually was, where each feature came from, whether our build already has it, and what to do about the rest.

## What was actually running on 2026-10-20

Evidence from the Steam folder (`d3d8.cfg`, `launcher\launch.txt`, the stray `generalszh.exe`, file timestamps):

| Layer | What it was | Evidence |
|---|---|---|
| **Engine** | **TheSuperHackers `weekly-2026-10-02`** (`30-g000127cad`, built 2026-10-09 by Mauller) — *not* the retail exe | `generalszh.exe` in the Steam folder embeds the build string; `launcher\launch.txt` (GenLauncher) points at it |
| **GenTool 7.6** | `d3d8.dll` proxy (2024-05-11), settings in `d3d8.cfg`: `pitch=37 camera=50 window=2 text=14 fps=16 render=0 brightness=0`; map-pack updater enabled (`Maps2024_1v1..4v4_ZH.zip`) | `[gentool76]` section |
| **GenPatcher add-ons** | `310_ExpandedLANLobbyMenu.big`, `340_ControlBarPro*.big` (incl. 2160p variants — you play at 3840×2160), `400_ControlBarHDEnglishZH.big`, `990_DecalsZH.big`, `CustomContentMaps.big`, `EdgeScroller.exe`, GenLauncher (`launcher\`), `GenToolUpdater.exe`; **modified originals** `Game.dat`, `WindowZH.big`, `PatchWindow.big`, `Data\INIZH.big` (Steam Verify restored these) | numbered `.big` files, `.bak` siblings, Verify's restore list |

So: **the engine you liked is the one we build.** Our `main` is that weekly plus 133 upstream commits (deterministic-math switches, netpacket fixes,
UI/text fixes). Everything engine-side carries over automatically. The things that *don't* carry over are GenTool's own features and
GenPatcher's data add-ons, and that is the list below.

Why GenTool is out: its `d3d8.dll` crashed our exe within seconds (GenTool's own thread, null read; see `tests/out/crash-20261021-153404`).
It worked with the weekly build, so it is not simply "any non-retail exe"; we have not chased why, because every feature we want from it
is either already native upstream or small to add. GenTool also exists mainly for C&C:Online anti-cheat/ranking, which we don't use.

## Feature → source → status in our build → action

| Feature you had | Came from | In our build today? | Action |
|---|---|---|---|
| **Control Bar Pro / Control Bar HD** (the nice command bar, 4K variants) | GenPatcher add-on `.big`s (author: xezon, same lead as upstream). Needed the full-viewport trick GenTool provided. | **Yes, natively supported:** upstream reads `GenTool/fullviewport.dat` or `-forcefullviewport` (`AddonCompat.cpp`, `ViewportHeightScale` in GameData.ini). The `.big` files themselves are not in a vanilla install. | **Ship them deliberately:** fetch the add-on archives at release time (like DXVK) into the game zip, pass `-forcefullviewport` from the launcher. Both machines then have identical `.big`s and the manifest enforces it. Verify the exact source/licence of the archives first (GenPatcher distributes them; originals on ModDB). |
| **Expanded LAN lobby menu**, **Decals** | GenPatcher add-ons (`310_`, `990_`) | No | Same mechanism as above, if wanted. Pure UI/visual `.big`s. |
| **Custom maps / map packs auto-updated** | GenTool updater (`Maps2024_*_ZH.zip`) + `CustomContentMaps.big` | No updater. Engine has `UseSystemMapDir` option. | Launcher feature: sync a pinned map-pack release into `Documents\...\Maps` so both machines match. Maps are community content, fine to redistribute; never EA assets. |
| **Higher camera** (`camera=50`), enforced equally in multiplayer | GenTool | **No.** Engine ceiling = `MaxCameraHeight` from GameData.ini × map factor; no multiplayer rule at all. | **AGENT-TODO #9:** camera height scale baked into the exe (default to tune by play-test), `Options.ini` override honoured offline, ignored in network games. Client-only, not in the CRC. |
| **Camera pitch** (`pitch=37`) | GenTool | GameData.ini `CameraPitch` only | Fold into #9 as a second scale/override. |
| **In-game overlay: FPS, clock, game timer, ping, money/min, player list** (GenTool "Display" menu, Insert key) | GenTool | **Mostly yes:** `Options.ini` `RenderFpsFontSize`, `SystemTimeFontSize`, `GameTimeFontSize`, `NetworkLatencyFontSize`, `PlayerInfoListFontSize`, `ShowMoneyPerMinute` (0 = off). Positions/fonts in `InGameUI.ini`. Upstream says its layout was modelled on GenTool's. No in-game menu to toggle them yet (`@todo` in OptionsMenu.cpp). | Ship sane defaults for both machines (code defaults, or a `config/` Options snippet the launcher merges). A toggle hotkey is a small later addition. |
| **Auto-saved replays** | GenTool (every game saved with a descriptive name) | **Yes:** `ArchiveReplays=yes` → `Documents\...\ArchivedReplays\YYYYMMDD_HHMMSS.rep` (Stubbjax 2025-10). Name lacks map/players. | Turn it on by default for both of us (it feeds `verify.ps1` baselines for free). Optional: add map + player names to the archive filename. |
| **Replay controls bar, speed, frame step, observer** | GenTool 8.0 (you had 7.6, so you may not have seen these) | **Yes:** `ReplayControls.cpp`, hotkeys `TOGGLE_PAUSE`/`STEP_FRAME`, `PlayerObserverEnabled`, `SaveCameraInReplays`/`UseCameraInReplays`. | Nothing. |
| **Windowed / borderless, window position presets** (`window=2`) | GenTool | `-win`, window centring/fit on monitor (upstream), resolution list fixes. Borderless-fullscreen: **verify** (`WinMain.cpp` uses `WS_POPUP` — check which mode). | AGENT-TODO #10: confirm a borderless 3840×2160 window works; pin via launcher args. |
| **FPS limiter** (`fps=16`) | GenTool | **Yes:** `FPSLimit` in Options.ini, `FramePacer`, uncapped supported. | Pin a default for both machines (PLAN Observability #5). |
| **Overlay text size** (`text=14`) | GenTool | **Yes:** the `*FontSize` keys + `ResolutionFontAdjustment`. | Defaults, as above. |
| **Brightness** | GenTool | Retail `Gamma` option. | Nothing. |
| **Render mode / fixes** (`render=0`) | GenTool proxy | Superseded: native MSAA, `AnisotropyLevel`, `TextureFilter`, shroud-edge fix, and DXVK as our reference renderer (PLAN Phase 5). | Nothing. |
| **Widescreen** | GenTool (its original headline feature) | **Yes**, native. | Nothing. |
| **Cursor lock, scroll tweaks, JPG screenshots** | GenTool | **Yes:** `CursorCaptureMode`, `ScreenEdgeScroll*`, `ScrollFactor`, `JpegQuality`. | Nothing. |
| **Modern hotkey presets** | GenPatcher (`CommandMap.ini` in user folder) | Engine supports `CommandMap.ini`; upstream added `SELECT_NEXT_IDLE_WORKER` (Ctrl+I). | Optional: ship a preset in `config/`. |
| **Game fixes / anti-cheat / online lobby & stats / upload mode** | GenTool (for C&C:Online) | Fixes: upstream has hundreds. Anti-cheat/stats: irrelevant for private 2-player. | Nothing. |
| **GenLauncher** | GenPatcher | Replaced by `launcher\ZHLauncher` (auto-update + manifest check + crash-dump surfacing). | Nothing. |
| **"Modified" `INIZH.big`** | **Resolved 2026-10-21: GenPatcher changed no rules.** It *deletes* `Data\INI\INIZH.big`, a stale pre-1.04 rule set (different weapon ranges, old Comanche damage type, other locomotor speeds) that the Steam/Chinese/Korean SKUs ship next to the real one; Steam Verify put it back. | **Handled in code:** upstream skips that file by name (`BIGFileSystem.cpp`, bobtista 2025-11-18, "to prevent CRC mismatches"). Our engine never loads it, so both machines run the root 1.04 rules whether the file exists or not. | Nothing. (`Game.dat`/`WindowZH.big`/`PatchWindow.big` were patched by GenTool/GenPatcher for window layouts and the Steam exe; irrelevant to our exe.) |

## Decisions this implies

1. **Vanilla data + deliberate, pinned add-ons.** Add-ons go in the release zip (fetched at build time from their upstream source, never
   committed), so the launcher delivers the identical set to both machines and `assets.sha256` proves it. Candidates, in order:
   Control Bar Pro/HD (+ `-forcefullviewport`), then map packs, then GeneralsGamePatch if its gameplay changes are wanted.
2. **GenTool is not coming back.** Its remaining unique features (camera scale with multiplayer enforcement, map updater) are ours to
   add; everything else is native upstream.
3. **Per-machine settings live in `Options.ini`; shared defaults live in code or the launcher.** Anything that must match (camera in
   network games, FPS limit, overlays) is set by the exe or launcher args, not by hand.

## Sources

- Local evidence: `C:\Program Files (x86)\Steam\...\Zero Hour\d3d8.cfg`, `launcher\launch.txt`, `generalszh.exe` build string, file timestamps.
- Upstream source: `Core/GameEngine/Source/Common/AddonCompat.cpp`, `CommandLine.cpp` (`-forcefullviewport`, `-loadreplay`, `-loadsave`),
  `OptionPreferences.cpp` (key list), `Recorder.cpp` (`archiveReplay`), `ReplayControls.cpp`, `InGameUI.cpp` counters.
- [Upstream wiki: New Configuration Options](https://github.com/TheSuperHackers/GeneralsGameCode/wiki/new_configuration_options),
  [Command Line Arguments](https://github.com/TheSuperHackers/GeneralsGameCode/wiki/switchers_arguments),
  [Releases](https://github.com/TheSuperHackers/GeneralsGameCode/releases).
- [GenTool documentation (GameReplays)](https://www.gamereplays.org/cnczerohour/portals.php?show=index&name=gentool&tab=9088168),
  [GenTool 8.0 notes](https://cncnz.com/gentool-version-8-0-released/), [GenTool 8.7/GenPatcher 2.07 notes](https://cncnz.com/genpatcher-and-gentool-updates-for-recent-tuc-builds/),
  [GenTool camera height thread](https://www.gamereplays.org/community/index.php?showtopic=1016641),
  [TheSuperHackers/GeneralsGamePatch](https://github.com/TheSuperHackers/GeneralsGamePatch).
