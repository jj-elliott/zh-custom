# Zero Hour custom build — agent briefing

Personal, non-commercial fork of EA's GPL-released *C&C Generals: Zero Hour* engine (upstream: TheSuperHackers/GeneralsGameCode).
Two players, co-op PvE skirmish over the internet. Goals in priority order: netcode that doesn't die on packet loss, a
decoupled/uncapped render rate, then visual and movement upgrades. Full plan: `docs/PLAN.md`. Your deferred tasks: `docs/AGENT-TODO.md`.

## Environment
- Windows, x86 **only**. The engine has inline asm; never attempt an x64 build.
- Build: `.\scripts\build.ps1` (Release/Win32 via CMake preset `win32`, output under `build\`). `-Clean` for a full rebuild.
- Playable install: path in `.zh-local.json` → `gameDir` (a copy of the Steam folder; the Steam copy is never touched).
- Verify loop: `.\scripts\verify.ps1` builds, copies the exe to `gameDir`, runs headless replays, diffs CRC against `tests\baseline`.
  Until AGENT-TODO #2 is done it will throw; do that first if you touch sim code.
- Launcher (C#, for the second player): `launcher\`. `dotnet publish launcher\ZHLauncher.csproj -c Release -r win-x64`.
- Releases: push a tag `v*` → `.github\workflows\release.yml` builds and publishes `zh-game-<tag>.zip`, `zh-launcher-<tag>.zip`, `zh-symbols-<tag>.pdb`.

## Tools you can run yourself (don't ask the human to paste output)
| command | what it gives you |
|---|---|
| `.\scripts\status.ps1` | environment report: branch, build age, toolchain, replay/baseline counts. Run first each session. |
| `.\scripts\build.ps1 [-Clean]` | compiler output on stdout; nonzero exit on failure |
| `.\scripts\run.ps1 [-Args ..] [-TimeoutSec N] [-Screenshot N]` | launches the game, harvests stdout, exit code, game logs, new crash dumps, optional screenshot into `tests\out\run-*\`; prints `OUTPUT_DIR=`. `-TimeoutSec 30` = smoke test ("still alive after 30 s" is a pass). |
| `.\scripts\verify.ps1` | build + headless replay + CRC diff vs baseline (scaffold until AGENT-TODO #2) |
| `.\scripts\symbolize.ps1 <dmp>` | crash dump → stack trace with file:line, using the build's PDB |
| `.\scripts\net-sim.ps1 -Loss 5 -DelayMs 80 -JitterMs 30` / `-Off` | inject packet loss/latency on UDP for solo netcode testing (two game instances over loopback) |
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

## Branches
`main` releasable only. Work on `net/*`, `render/*`, `move/*`, `backend/bgfx`. Tag only from `main`.

## Where things live (fill in `docs/CODE-MAP.md` on first session — AGENT-TODO #1)
Rough starting points: engine core `Core/`, Zero Hour game `GeneralsMD/Code/`, W3D renderer `Core/Libraries/Source/WWVegas/WW3D2/`,
network `GeneralsMD/Code/GameEngine/Source/GameNetwork/`, logic/AI `GeneralsMD/Code/GameEngine/Source/GameLogic/`. Verify against the tree; upstream moves things.

## Working style
- Before any sim or net change: read `docs/PLAN.md` for the phase, then the relevant part of `docs/CODE-MAP.md`.
- Explain a change's determinism impact in the commit message when it touches sim code.
- When a plan item turns out to already exist upstream, note it in `docs/PLAN.md` and skip it rather than reimplementing.
- Update `docs/AGENT-TODO.md` as items complete; it is the handoff between sessions.
