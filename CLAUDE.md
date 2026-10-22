# Zero Hour custom build — agent briefing

Personal, non-commercial fork of EA's GPL-released *C&C Generals: Zero Hour* engine (upstream: TheSuperHackers/GeneralsGameCode).
Two players, co-op PvE skirmish over the internet. Goals in priority order: netcode that doesn't die on packet loss, a
decoupled/uncapped render rate, then visual and movement upgrades. Full plan: `docs/PLAN.md`. Your deferred tasks: `docs/AGENT-TODO.md`.

## Environment
- Windows, x86 **only**. The engine has inline asm; never attempt an x64 build.
- Build: `.\scripts\build.ps1` (Release/Win32 via CMake preset `win32`, output under `build\`). `-Clean` for a full rebuild.
- Playable install: path in `.zh-local.json` → `gameDir` (a copy of the Steam folder; the Steam copy is never touched).
- Verify loop: `.\scripts\verify.ps1` builds, then re-simulates every replay in `tests\replays` headless with the built exe; the replay's
  embedded CRCs are the baseline, any divergence fails. `-Import -Name x` promotes the last played game to a new baseline. Run it before
  and after every sim change; a deliberate sim change must come with re-recorded baselines.
- Launcher (C#, for the second player): `launcher\`. `dotnet publish launcher\ZHLauncher.csproj -c Release -r win-x64`.
- Releases are attended, from this machine: `.\scripts\release.ps1 -Tag vX.Y.Z` (clean `main` in sync with origin → `verify.ps1` gate →
  `package.ps1` → tag → `gh release create` with `zh-game-<tag>.zip`, `zh-launcher-<tag>.zip`, `zh-symbols-<tag>.pdb`). `release.yml` is a
  manual-dispatch fallback that packages with the same `package.ps1`. Repo `jj-elliott/zh-custom` is public; the launcher needs no token.

## Tools you can run yourself (don't ask the human to paste output)
| command | what it gives you |
|---|---|
| `.\scripts\status.ps1` | environment report: branch, build age, toolchain, replay/baseline counts. Run first each session. |
| `.\scripts\build.ps1 [-Clean]` | compiler output on stdout; nonzero exit on failure |
| `.\scripts\run.ps1 [-Args ..] [-TimeoutSec N] [-Screenshot N]` | launches the game, harvests stdout, exit code, game logs, new crash dumps, optional screenshot into `tests\out\run-*\`; prints `OUTPUT_DIR=`. `-TimeoutSec 30` = smoke test ("still alive after 30 s" is a pass). |
| `.\scripts\verify.ps1 [-SkipBuild] [-Replay x.rep] [-Import -Name x]` | build + headless replay CRC check of `tests\replays\*.rep`; per-replay verdicts, exit code = failures, auto-postmortem on crash |
| `.\scripts\postmortem.ps1 [-Dump x.dmp]` | **crash? run this first.** Newest minidump → exception, faulting module (+ ours/windows/third-party verdict), symbolized stack, logs, all in `tests\out\crash-*\summary.txt`. `run.ps1` calls it automatically when a run produced a dump. |
| `.\scripts\symbolize.ps1 <dmp>` | raw cdb output for a dump (used by postmortem) |
| `.\scripts\stage-install.ps1` | re-stage `gameDir` from the Steam folder: vanilla files, then our verified add-ons (`addons\addons.json`), our exe + PDB, regenerates `assets.sha256`. The way back to a known state. |
| `.\scripts\fetch-addons.ps1 [-ImportFrom dir]` | fills/verifies the add-on cache (`C:\Games\ZH\addons`) against pinned sha256s; CI uses it to put the same files in the release zip |
| `.\scripts\net-sim.ps1 -Loss 5 -DelayMs 80 -JitterMs 30` / `-Off` | inject packet loss/latency on UDP for solo netcode testing (two game instances over loopback) |
| `python -I scripts\netlog-summary.py host.log peer.log` | digest of `-netlog` session logs (`gameDir\logs\net-*.log`): stalls, latency, jitter, run-ahead, retries, stall↔retry correlation. Reference logs: `tests\netlogs\` |
| `dotnet publish launcher\ZHLauncher.csproj -c Release -r win-x64` | builds the launcher |

Prefer these over ad-hoc commands so output lands in predictable places. If a script is wrong, fix the script; that is part of the job.
Things only a human can do: play an actual skirmish and say how it *feels*, and test against the second machine. Ask for those explicitly and narrowly.

## Hard rules
- **Never commit game assets.** No `.big`, no `Data\`, nothing from the Steam folder. `assets.sha256` (hashes only) is fine.
- **Sim code must be deterministic.** Anything under the game logic (`GameLogic`, AI, pathfinding, physics, RNG) runs in lockstep on both machines.
  No `float` results that depend on compiler flags, no wall-clock, no unordered-container iteration order affecting outcomes, no `std::rand`.
  Run `verify.ps1` before and after every change in those areas.
- **Render changes go through the device layer** (`DX8Wrapper` / `DX8Renderer` in `WW3D2`). Never call D3D from gameplay or UI code.
- **Don't fight upstream.** Keep changes in small, separable commits so `git merge upstream/main` stays cheap. Prefer adding files to editing hot ones.
- DXVK (`d3d8.dll`/`d3d9.dll` in the game folder) is the reference renderer. If a visual bug appears, bisect with DXVK removed before blaming engine code.
- **No GenTool, no GenPatcher** in `gameDir` or on the second machine. GenTool's `d3d8.dll` hooks the retail exe at fixed addresses and crashes
  any rebuilt exe (first crash of this project, 2026-10-21). GenPatcher alters `.big` files, including INI rules, which breaks lockstep unless both
  sides match exactly. Baseline is a vanilla Steam install; add-ons we want go through the release zip deliberately. `stage-install.ps1` enforces this.
- Crash dumps land in `%USERPROFILE%\Documents\Command and Conquer Generals Zero Hour Data\CrashDumps\` (upstream's MiniDumper), not the game dir.

## Branches
`main` releasable only. Work on `net/*`, `render/*`, `move/*`, `backend/bgfx`. Tag only from `main`.

## Where things live
`docs/CODE-MAP.md`: 22 verified entries with entry-point functions and which plan phase touches each. Shared code was hoisted by upstream
into `Core/` (network layer, pathfinder, view, device layer live there now); Zero Hour specifics in `GeneralsMD/Code/`. Ignore `Generals/`.
`docs/FEATURE-PARITY.md`: what the GenTool/GenPatcher setup had, where each piece came from, and what we ship instead.

## Working style
- Before any sim or net change: read `docs/PLAN.md` for the phase, then the relevant part of `docs/CODE-MAP.md`.
- Explain a change's determinism impact in the commit message when it touches sim code.
- When a plan item turns out to already exist upstream, note it in `docs/PLAN.md` and skip it rather than reimplementing.
- Update `docs/AGENT-TODO.md` as items complete; it is the handoff between sessions.
