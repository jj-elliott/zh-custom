# Agent TODO — deferred until running inside the repo

These need the actual source tree or a Windows machine, so bootstrap.ps1 left them to you. Work top-down; tick off here.

- [x] **1. `docs/CODE-MAP.md`** — written 2026-10-21, 22 entries with verified paths, entry-point functions, and which plan phase touches each.
      Notable: `Mass` is parsed by `PhysicsBehavior`, not `Locomotor`; `class Network` is defined inside `Network.cpp`; world XY of drawables is
      *not* interpolated yet; the CRC-mismatch path ends the game rather than disconnecting; `DebugLogFile` only exists in `*-releaselog` builds.
- [x] **2. Make `scripts/verify.ps1` real** — done 2026-10-21. First baseline `tests/replays/skirmish-01.rep` (6:20 of game, human-recorded,
      re-simulates in 3 s, all CRCs match). Harness details: runs the built exe from `build\` with `-useCwd -headless -jobs N -replay <abs>`
      per replay in `tests/replays/`, parses stdout into per-replay verdicts (`CRC MISMATCH frame N` / `CANNOT OPEN` / `ok`), exit code = failures,
      auto-postmortem on crash. `-Import -Name x` copies the game's last replay into `tests/replays/x.rep` as a baseline. No CRC log file is
      needed: the replay carries the CRCs (see Findings). **Upstream's `GeneralsReplays` (retail 1.04 recordings) mismatch at the first check
      (frame 110) on the VS2022 build** — expected, CI only certifies VC6 builds against them; they are not usable as our baseline.
      Still open (not blocking): a CI replay job needs game data we cannot publish — upstream uses a private, encrypted trimmed data set in R2;
      do the same with a private release asset later, or rely on the local loop. More baselines wanted: one per faction pair, one long (20+ min)
      game, one 2-player LAN/online game (exercises the network command path).
- [x] **2b. Smoke-test `scripts/run.ps1`** — done 2026-10-21: 30 s windowed run survives, screenshot at 20 s shows the intro movie rendering
      (use `-Args "-win -quickstart"` to skip to the menu). Harvest now covers the Documents user-data folder (DebugLogFile*, ReleaseCrashInfo.txt,
      CrashM*.dmp) and auto-runs `postmortem.ps1` when a run produced a dump. Release builds don't write DebugLogFile; that needs a `-releaselog`-style preset.
- [x] **3. PDB confirmed:** `build\win32\GeneralsMD\Release\generalszh.pdb` (54 MB) is emitted by the stock `win32` preset; `stage-install.ps1` copies it beside the exe.
- [ ] **4. Check whether upstream already decoupled render from the 30 Hz sim** (changelog, search for interpolation / frame-rate uncap).
      Record the finding in `docs/PLAN.md` Phase 5 step 2.
- [x] **5. Crash dump writer** — already upstream (see findings below). Our side done 2026-10-21: launcher surfaces dumps from the Documents
      CrashDumps folder too; `scripts/postmortem.ps1` turns the newest dump into a verdict. First use: GenTool's `d3d8.dll` crash.
- [~] **6. `-netlog <path>` flag** — implemented 2026-10-21 on `net/instrumentation`: `Core/GameEngine/Include/GameNetwork/NetLog.h` (+cpp), a
      pure observer with one-line hooks in `Network.cpp` (start, per-update stall/ready state, run-ahead applied, CRC mismatch),
      `ConnectionManager.cpp` (run-ahead computed with its inputs), `Connection.cpp` (ack latency samples, retries), `Transport.cpp`
      (packets/bytes per second), `DisconnectManager.cpp`. One `sec` line per second plus `stall`/`runahead`/`crc`/`event` records.
      Launcher passes it when `release.json` `NetLog` is true (shipped on) or `launcher.json` `Debug`; files land in `gameDir\logs\net-*.log`,
      last 20 kept. Shipped in **v0.2.0** (2026-10-21) together with windowed edge scrolling. **Open:** read a real two-machine session log;
      decide Phase 4 step 2 (jitter term) from it. Not in the CRC. Launch args are now `-win -nologo` (skip logo + intro movie, keep the
      animated shell map); the human prefers that over `-quickstart`.
- [x] **7. `/LARGEADDRESSAWARE`** — set 2026-10-22 (`GeneralsMD/Code/Main/CMakeLists.txt`); PE characteristics 0x122; CRC unchanged.
- [~] **16. Menu UI system** (`render/ui`, spec `docs/UI-DESIGN.md`, mockups linked there). Done: primitives (`Display::drawRounded*`,
      `drawDisc`, `drawPolyline` in `W3DDisplay`), `UIStyle` tokens/renderers, `-uitest` component sheet, `-classicui` switch.
      Step 2 passed 2026-10-22. Step 3 done the same day: `config/Fonts` (Barlow, Barlow Semi Condensed, Saira Stencil One, OFL),
      `UIFonts` registration + legacy name substitution in `FontLibrary::getFont`, `UIStyle::Text` small caps.
      Step 4 built 2026-10-23: `UILayout` (layout descriptions), `UIGadgets` (push button + static text renderers), hooks in the
      W3D push-button and static-text draw functions, `config/UI/Layouts/MainMenu.layout`. **Next:** main menu signed off 2026-10-23. Step 5 (skirmish setup) built the same day:
      panel/rule/ruler decorations, entry / dropdown / checkbox / list / slider renderers (chrome first, stock text drawing
      kept with cleared colours), `at:` and `text:` window matching, `-uiscreen`. A missing per-frame text-pool reset
      exhausted DXVK memory in ~30 s on real screens (fixed: `UIStyle::Text::beginFrame` from `InGameUI::postDraw`, pool
      capped). Review round 1 on skirmish (2026-10-23) fixed: combo chevron geometry, medal aspect, radio segments, map selection
      (`SkirmishMapSelectMenu.layout`), `$Name` and `include`, `-uiscreen click:`. Round 2 (2026-10-24): managed entries centre
      their text vertically (`W3DTextEntry.cpp`, plain draw), Color column widened to 140 units; `build.ps1` now stages exe,
      PDB and the `config` folder into `gameDir` after a good build (skipped with a notice when the exe is locked); passed.
      Round 3 (2026-10-24): list scroll bar 24 units wide, thumb proportional with a 48-unit minimum (`GadgetListBox.cpp`
      adjustDisplay, `GadgetVerticalSlider.cpp` GGM_RESIZED, guarded by the layout style), thumb in `text.secondary`; the
      main menu frame returns as `band` decorations (`MainMenu.layout`). Sign-off on those pending (human look), then the
      remaining shell screens, reusing the skirmish blocks via `include` for the LAN / online game-setup screens.
      Known: the shell shows its menu only once the window has focus and the pointer has moved; `run.ps1 -Screenshot` now
      focuses the window and nudges the cursor four seconds before the shot, which steals focus: only run it when the human
      has said the screen is free (they may be gaming).
      `render/textures`: texture-pack scripts and the earlier gadget/anchoring drafts (superseded).
- [~] **8. First netcode fix: adaptive retry timer** — implemented 2026-10-21 on `net/retry` (`Connection.cpp` `processAck`): retry after
      1.5× max(current sample, running average), floor 80 ms, cap 500 ms; 250 ms before the first ack (was a fixed 2000 ms). No backoff yet.
      The netlog `sec` line now carries `retry_ms`. **Measured 2026-10-22 on the solo rig at 4% loss:** 0.6% / 0.1% frozen vs 11%, no stall
      over 1 s except the 30→4 run-ahead settle at game start (`Tests/netlogs/2026-10-22-solo-loss4-*`). Shipped v0.3.0. **Open:** confirm on
      a real session; possible refinement: floor the retry at 2× the frame grouping interval so loopback-fast links don't resend before the
      piggybacked ack can arrive (cosmetic: extra tiny packets only).
- [~] **15. Solo netcode rig** — built 2026-10-21 on `net/retry`: `scripts/solo-net.ps1` starts two instances (`-multiinstance`; the second
      gets `Options_Instance02.ini`) bound to 127.0.0.1 / 127.0.0.2 via `-lanip` (no port offset needed: each binds its own loopback IP),
      discovering each other through `-lanpeer`, with upstream's loss/latency simulation now compiled in release (`Transport.h`
      `RTS_NETSIM`; `-packetloss`/`-latAvg`/`-latAmp`/`-latPeriod`/`-latNoise`) applied to instance B. Each writes `logs\net-solo-A/B.log`.
      First run 2026-10-22: both instances up, host/join/play worked, logs produced. The lobby/host/join clicks stay human. `net-sim.ps1`
      (clumsy) is now the fallback.

Added 2026-10-21 from `docs/FEATURE-PARITY.md` (what the GenTool/GenPatcher setup had that we must keep):
- [x] **9. Camera height scale** — done 2026-10-21, merged to `main` (3a417b362): `Core/GameEngine/Include/Common/CameraHeightScale.h` (+cpp),
      `DefaultScale = 1.5` (310 → 465) for everyone in network games, `Options.ini` `CameraHeightScale` honoured offline. CRC-verified no-op;
      human play-tested: "feels right". Pitch left at retail 37.5 (GenTool's 37 was the same).
- [x] **10. Add-ons in the release zip** — done 2026-10-21. Archives are assets of release `addons-v1` in `jj-elliott/zh-custom` (public),
      fetched by `fetch-addons.ps1` (gh), layered by `stage-install.ps1`, packaged by `package.ps1`. Map pack `maps-v1` published, installed by
      the launcher. **Still open (small):** test `310_ExpandedLANLobbyMenu` against upstream's lobby; confirm borderless 3840×2160 and pin
      launcher args.
- [x] **Phase 2/3 (2026-10-21):** repo `jj-elliott/zh-custom` public; releases `v0.1.0`, `v0.1.1` (launcher fix), `addons-v1`, `maps-v1`
      (prereleases). Attended release path `scripts/release.ps1` proven; `release.yml` is a dispatch-only fallback. Launcher verified on this
      machine: dev-local → v0.1.0 → v0.1.1 → v0.1.2 updates, manifest check, map-pack install, DXVK 2.5.3 active. **v0.1.2 (camera 2.0x)
      makes the kit send-once:** the launcher self-updates (proven: a v0.0.0-stamped launcher replaced itself with v0.1.2 and restarted) and
      takes `MapPack`/`Args` from `release.json` shipped in the game zip (`config/release.json`). Dad kit `out/zh-dad-kit-v0.1.2.zip` handed
      to the human; it is the last kit that ever needs sending. **v0.1.3: first-run setup** — run from any folder, the launcher finds the Steam
      install (registry, then Steam library folders), copies it to `C:\Games\ZeroHour-Custom` minus GenTool/GenPatcher leftovers, drops itself
      in, makes a desktop shortcut and hands over (tested from an empty folder: ~3 s copy on NVMe, then update + launch). Setup instructions
      are now "unzip anywhere, double-click". **Second-machine test passed 2026-10-21:** the human and their dad played end to end via
      the kit. Observations: (a) over Tailscale the LAN lobby did not discover the peer; they used Direct Connect (by IP), which works →
      #13; (b) periodic hitches during play → Phase 4 is worth it, start with #6 instrumentation.
- [ ] **14. Code-sign the launcher and the exe (low priority).** The second machine runs Windows 11 Smart App Control, which blocked the
      self-updated (unsigned) launcher; the workaround is turning SAC off (documented in `launcher/README-SETUP.txt`). Signing (e.g.
      Azure Trusted Signing, ~$10/month, identity-verified) in `package.ps1` would remove the need. Only if the workaround is unacceptable.
- [~] **13. LAN discovery over Tailscale** — implemented 2026-10-21 on `net/retry`: `-lanpeer <ip>` (repeatable) makes `LANAPI::sendMessage`
      unicast every lobby broadcast to those addresses too; the launcher passes `release.json` `LanPeers` (both tailnet IPs:
      vision-1 100.66.50.86, admiral 100.91.103.10). Confirmed 2026-10-22 on the loopback rig (B listed A's game via the -lanpeer unicast, joined from the list). **Open:** the same over
      the real tailnet on the next session (release.json carries both IPs).
- [x] **11. Shared defaults** — done 2026-10-22 (v0.3.1): `release.json` `Options` is written into each machine's `Options.ini` by the
      launcher before every launch (`launcher.json` `OptionsOverride` wins per machine): VeryHigh detail, full textures, 5000 particles,
      shadows/decals/cloud/light maps, `ArchiveReplays=yes`, DynamicLOD off. Engine defaults: uncapped render with the logic clock at
      30 Hz, 15% font adjustment, edge scrolling in windowed mode. DXVK config: vsync, triple buffering, sample-rate shading.
      Optional later: map + player names in the archived replay filename; overlay font sizes.
- [ ] **12. Netcode test flags already exist** in debug builds: `-packetloss`, `-latAvg`, `-latAmp`, `-latPeriod`, `-latNoise` (`CommandLine.cpp`).
      Expose them in release (or use a `win32-profile` build) and make `net-sim.ps1` prefer them over clumsy.

## Findings from source inspection (2026-10-21, before first build) — read before starting the items above

- **#2 verify.ps1:** upstream's mechanism is `generalszh.exe -jobs N -headless -replay <glob-or-name>`. Replays are resolved relative to
  `%USERPROFILE%\Documents\Command and Conquer Generals Zero Hour Data\Replays\`. The replay file *is* the baseline: it embeds a CRC every
  `REPLAY_CRC_INTERVAL` (=100) logic frames, playback compares and prints `CRC Mismatch in Frame N` to stdout, exit code 1 on any mismatch
  (`Core/GameEngine/Source/Common/ReplaySimulation.cpp`, `GeneralsMD/.../Common/Recorder.cpp` `handleCRCMessage`). So no separate `.crc` log is
  needed: `tests/replays/*.rep` recorded with a known-good build + exit code + stdout grep. Caveats: (a) CI only validates retail replays on
  VC6 presets, so the VS2022 `win32` build may not pass upstream's `GeneralsReplays` submodule (1.8 MB, not initialised by bootstrap; try it once);
  (b) every deliberate sim change (Phase 7) and every upstream merge that touches sim invalidates baselines → re-record. `-replayCRCInterval`
  only exists under `DEBUG_CRC`; exposing it in release would give per-frame granularity for our own baselines.
- **#2b run.ps1:** add `%USERPROFILE%\Documents\Command and Conquer Generals Zero Hour Data\` to the harvest list (DebugLogFile*.txt, CrashDumps\, Replays\LastReplay.rep).
- **#3 PDB:** `win32` inherits `default` → `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded` (/Z7) and `cmake/compilers.cmake` adds `/DEBUG` to
  Release link flags, so a `generalszh.pdb` should appear beside the exe. Confirm after the first build.
- **#4 done upstream** — see PLAN.md Phase 5 step 2.
- **#5 crash dumps: already upstream.** `Core/Libraries/Source/MiniDumper/` behind `RTS_CRASHDUMP_ENABLE` (ON by default), hooked in `WinMain.cpp`
  `UnHandledExceptionFilter`, writes minimal+full dumps to `<user data dir>\CrashDumps\` (Documents folder, **not** the game dir). Remaining work is
  only on our side: point the launcher's dump lookup and `run.ps1`'s harvest at that folder. Don't write a second dumper.
- **#7 LAA:** no `LARGEADDRESSAWARE` anywhere in upstream CMake; still ours to add (`target_link_options(... /LARGEADDRESSAWARE)` on `z_generals`).
- Upstream has an `AI_POLICY.md`: autonomous agents may not open PRs or comments there. Our fork is private, but anything upstreamed must be hand-written by the human.

Done items: move them down here with the commit hash.
