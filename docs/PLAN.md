# Zero Hour custom build pipeline

Goal: you compile, GitHub builds and publishes, your dad double-clicks one launcher and is always on the same build as you.

## Fork choice (corrected from the voice chat)

| | TheSuperHackers/GeneralsGameCode | olcayseygan/CnCGeneralsZH-Reforged |
|---|---|---|
| Arch | x86 only | x86 only (x64 explicitly rejected; inline asm) |
| Renderer | DX8 (+ community fixes) | DX8 via d3d8to9 shim, **not** D3D11 |
| Build | `cmake --workflow --preset win32`, deps auto-fetched | Plain CMake, **you must hand-drop** zlib 1.1.4, LZH-Light, DX8 SDK, GameSpy SDK, d3d8to9 |
| CI | Has working GitHub Actions | None |
| Multiplayer | Actively played/tested, replay-based desync tooling | "Online and LAN play are untested" |
| Uncapped FPS | Check changelog; not guaranteed | **Done** (render uncapped, sim on own clock) |
| AI/pathfinding fixes | Many | Many (different set) |

Default here: SuperHackers. Reason: it is the only one that builds hands-free in CI and is proven over a network, which are your two hard requirements. Reforged's frame-rate decoupling can be cherry-picked across later (same GPL, same parent repo). `bootstrap.ps1 -UpstreamUrl` flips the base if you want to experiment.

## Layout

```
scripts/bootstrap.ps1          one-shot: toolchain → clone → build → staged dev install → (optional) repo
.github/workflows/release.yml  on `git tag vX.Y.Z && git push --tags`: builds exe + launcher, attaches zips to a release
launcher/                      ZHLauncher: checks releases/latest, swaps exe, launches
```

## Phase 1 — local build (tonight)

Elevated PowerShell:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\bootstrap.ps1
```

If it can't find Steam's folder: `-SteamZHPath "<folder containing generals.exe>"`.
Result: `C:\Games\ZH\ZeroHour-Dev\generalszh.exe`. Run it, start a skirmish, confirm it renders. That's the gate for everything else.

Iteration loop afterwards (x86 Native Tools prompt, or let the script do it):
```
cd C:\Games\ZH\GeneralsGameCode
cmake --build build/win32 --target z_generals
copy build\win32\GeneralsMD\Release\generalszh.exe C:\Games\ZH\ZeroHour-Dev\
```

## Phase 2 — pipeline

```powershell
.\scripts\bootstrap.ps1 -SkipInstall -CreateRepo -RepoName zh-custom
```
This renames the fork remote to `upstream`, copies `.github/` and `launcher/` into the tree, creates a **private** repo and pushes. First release:
```
git tag v0.1.0 && git push origin v0.1.0
```
Watch the Actions tab; ~10 min later the release has `zh-game-v0.1.0.zip` and `zh-launcher-v0.1.0.zip`.

Pull community fixes any time: `git fetch upstream && git merge upstream/main`.

Keep your own work on branches off `main`; tag from `main`.

## Phase 3 — dad's machine (one-time, ~10 min)

1. Copy his Steam Zero Hour folder to e.g. `C:\Games\ZeroHour-Dev` (Steam's copy stays untouched as a fallback).
2. Drop `ZHLauncher.exe` and `launcher.json` into that folder.
3. In `launcher.json` set `Repo` and a **fine-grained PAT**: GitHub → Settings → Developer settings → Fine-grained tokens → repository access = `zh-custom` only, permission = Contents: Read. Set a long expiry. (Private repo assets need auth; this token can do nothing except read that one repo. If you'd rather, make the repo public and leave `Token` empty.)
4. Shortcut to `ZHLauncher.exe` on the desktop. Done. From now on he just double-clicks it.

Launcher behaviour: compares `BUILD_VERSION.txt` to `releases/latest`; downloads and overwrites if different, keeps `generalszh.exe.prev` as a one-step rollback; if offline it launches whatever is installed.

Because you both always run the tagged exe, every setting you bake into code is identical on both ends, so lockstep config drift can't happen.

## Phase 4 — netcode (first feature)

Read first: `GeneralsMD/Code/GameEngine/Source/GameNetwork/` (FrameData, NetworkInterface, ConnectionManager, FrameDataManager). The wiki's `crcdiff` page is the desync tooling.

Context: StarCraft II, AoE4, Factorio and Stormgate are all still deterministic lockstep. Nobody replaced lockstep for RTS; they changed what happens *around* it. The sequence below copies those techniques in order of value-per-effort.

0. **Transport first (no code).** Hamachi adds a relay hop and jitter. Swap to Tailscale (WireGuard, direct peer-to-peer) or a plain port-forward before measuring anything.
1. **Instrument, don't guess.** Log: negotiated frame delay (run-ahead), per-frame stall time waiting on the peer, RTT *and* jitter, and the CRC mismatch frame + which subsystem's CRC diverged. Play one real session and read the log.
2. **Jitter-aware run-ahead.** ZH derives run-ahead from latency only. Pad it to cover the measured jitter envelope (p95 inter-packet gap), with a sane floor for the 2-player case. Bake it in code so both ends match automatically.
3. **State resync instead of termination (Factorio model).** On CRC mismatch: host serialises game state (the save-game path already exists), ships it, guest loads and rejoins at that frame. ~2-second hitch, no more dead games. Host is the source of truth; this is already how Factorio handles desync.
4. **Local input acknowledgment (the SC2 trick).** Respond to the player's input *immediately* on the UI layer: selection highlight, unit voice bark, order marker, start of turn animation. The actual command still executes N ticks later in lockstep. This is why SC2 feels responsive at the same real latency, and ZH currently gives zero feedback until the command clears the wait. Cheap, pure presentation layer, biggest *felt* improvement.
5. **Catch-up instead of stall (Factorio).** When a client falls behind, run the sim at 2–3× until caught up rather than freezing. With the decoupled render loop from Phase 5, a hitch becomes a brief fast-forward.

Explicitly skipped: GGPO-style rollback. Needs per-tick snapshot/restore of the full sim; ZH has no cheap state copy. Full server-authoritative rewrite stays a stretch goal; steps 3–5 get most of the benefit.

## Phase 5 — rendering (near term)

Reality check: every ZH fork is x86 (inline asm) and D3D8 fixed-function. No fork has D3D11. Visual wins come from the layers *around* the D3D8 path, not from replacing it yet.

1. **DXVK (zero engine code).** `release.yml` now bundles DXVK's `d3d8.dll` + `d3d9.dll` (native D3D8 support, no d3d8to9 shim) and a `dxvk.conf` forcing 16× AF and swapchain MSAA. Modern Vulkan path on both machines, pinned version, ships in the zip. Requires a Vulkan-capable GPU on your dad's side; check.
2. **Decouple render from the 30 Hz sim.** If upstream hasn't, port Reforged's commits (CHANGELOG.md, "rules on their own clock"). Interpolate `Drawable` transforms between logic frames. Needed for Phase 4 step 5 *and* Phase 6.
3. **Textures.** AI-upscale the `.dds` in the `.big` archives (ESRGAN on the 5090), re-export with mipmaps into a higher-priority `.big`. Reforged ships 481 originals at 4× as reference. Set the exe LAA flag (`/LARGEADDRESSAWARE`) for 4 GB address space; x64 is not needed for this.
4. **Native D3D9 in `DX8Wrapper`.** Contained change, removes the translation layer, unlocks Shader Model 3 for normal maps and a post pass without touching fixed-function code. Optional if Phase 6 is going to happen anyway.
5. Models: pilot one static building through an image-to-3D generator before touching any animated unit.

## Phase 6 — modern render backend (long term)

The "keys to everything" step: cascaded shadow maps, deferred lighting (explosions light the scene), normal maps, real post-processing, texture streaming. Not a renderer rewrite: a **second backend behind W3D's `DX8Wrapper`/`DX8Renderer` device layer**, using **bgfx** (D3D11/12/Vulkan, shader cross-compile, x86-fine). Scene graph, meshes, animation, terrain, particles stay as they are.

Milestones, in order, first two deliberately invisible:
1. bgfx backend renders the main menu pixel-identical to the DXVK baseline.
2. Fixed-function emulation complete (texture stages, per-vertex lighting, fog, alpha test). Use DXVK's fixed-function shader generator as the reference (zlib licence, GPL-compatible). Diff against DXVK screenshots from the regression harness.
3. First new feature: depth pre-pass + cascaded shadow maps replacing stencil volumes.
4. G-buffer + deferred lighting. Then normal maps, then post.

Budget: 3–6 months of evenings. Start only after Phases 1–5 are shipped and in use.

## Phase 7 — pathfinding and unit movement (long term, after Phase 4)

Target feel: StarCraft 2. The A-star pathfinder stays; the work is in the layer between "where" (pathfinder) and "how" (`Locomotor`). Everything here runs inside the sim, so it must be deterministic: fixed-point or controlled float, verified by the CRC half of the regression harness. Ordered so each step is useful on its own and the cheap ones come first.

1. **Yield rule (~50 lines, no new maths).** An idle unit that is blocking a moving unit steps aside instead of making the mover wait. Lives in the AI idle state. Kills the single most common stall.
2. **Push priority.** Rank contenders when two units collide: moving beats idle, explicit player order beats auto-move (harvester returns, guard), then heavier `Mass` from the INI beats lighter. Lower priority yields; ties go to whoever arrived first. Prevents mutual-yield deadlock and lets an Overlord clear a path through Red Guard rather than wait behind them.
3. **Group pass-through + stuck fail-safe.** Units moving in the same direction ignore collision with each other (SC2's "flow like liquid through a ramp"). A unit stuck past ~2 s temporarily drops allied collision for itself only, replacing the existing teleport-nudge. Never a global collision toggle.
4. **Steering / local avoidance layer.** ORCA-style velocity adjustment per tick, inserted between the pathfinder's next waypoint and the `Locomotor`. Neighbour query from the existing partition manager. Re-request the route if steering pushes a unit more than a cell or two off path. Infantry first; it's the easy case.
5. **Vehicle constraints.** Run avoidance on a circular stand-in, then feed its velocity to the real locomotor as a *target* clamped by turn rate and acceleration, so tanks turn toward the heading rather than slide. Reverse rule: if the desired direction is >~120° behind and the distance is short, back up. Optionally cheat like SC2 and let vehicles rotate in place faster than physics allows.
6. **Flow fields for large groups** (optional). One field per shared destination instead of per-unit searches, plus a formation or crowd target so 50 units don't converge on a single point.

Budget: steps 1–3 a couple of weekends; 4 another couple; 5 a month or two of evenings; 6 another month. Cheaper than Phase 6 and independent of it; can run in parallel on a `move/*` branch.

## Adjustments pulled forward to make Phase 6 cheap later

- **Device-layer discipline from day one.** Every render change in Phases 4–5 goes through `DX8Wrapper`; never call D3D directly from gameplay or UI code (the local-input-acknowledgment work in Phase 4 will tempt you). Grep for `IDirect3D` outside `WW3D2/dx8*` in code review.
- **Regression harness early (Phase 2, not Phase 6).** Upstream has headless replay testing (`replay_testing`, `crcdiff` wiki pages); Reforged has a 38-second headless skirmish. Set up: fixed-seed replay → run headless → CRC per frame → screenshot at fixed frames. Netcode work uses the CRC half immediately; the backend uses the screenshot half later. Add it as a second job in `release.yml`.
- **Decoupled render loop before any visual work** (Phase 5 step 2). Both catch-up mode and a new backend assume the renderer owns its own clock.
- **DXVK as the permanent reference renderer.** Keep it switchable (DLL present or not) even after Phase 6 so a visual bug can always be bisected to "engine" vs "backend".
- **Branch layout:** `main` (releasable), `net/*`, `render/*`, `move/*`, `backend/bgfx` long-lived. Tag only from `main`.

## Observability — do during Phase 2/3, before any netcode or render work

A two-person project dies on "it crashed, dunno". These make every problem reproducible.

1. **Crash dumps with symbols.** Build with `/Zi` + `/DEBUG` so Release emits `generalszh.pdb`; upload the PDB as a separate release asset (not in the game zip). Upstream wiki has a `crashdumps` page. Launcher keeps the last `.dmp` in `crashdumps/`. Dump + PDB = file and line.
2. **Session logs.** `-netlog` flag writes the Phase 4 instrumentation (run-ahead, stall time, RTT/jitter, CRC mismatch frame) to `logs/<timestamp>.log`. Launcher passes it when `launcher.json` has `"Debug": true`. After a bad session: send one file.
3. **`.big` asset hashing.** Release carries `assets.sha256`. Launcher hashes the `.big` files and refuses to launch on mismatch. Asset mismatch desyncs exactly like a code mismatch; a GenPatcher install on one side and vanilla on the other will do it.
4. **Pre-flight check in the launcher.** Vulkan present, assets hashed, exe version matches tag, free VRAM for the texture pack. One-line verdict before launch.
5. **Shipped config.** Pin resolution, windowed mode, game speed and DXVK settings in files the release carries, so both machines are identical without touching in-game menus.
6. **Replays on by default.** Every session saved. Free bug repro: feeds straight into the headless harness.

Priority if time is short: 1 and 3.

What's already wired: the launcher does the Vulkan check, `assets.sha256` verification (`AllowMismatch` in `launcher.json` to override), surfaces a recent `crashdumps/*.dmp`, and adds `-netlog` when `Debug` is true. `release.yml` ships the PDB as `zh-symbols-<tag>.pdb`, copies `assets.sha256` and anything under `config/` into the game zip. Still yours: run `scripts/make-asset-manifest.ps1` once and commit the result; make sure the Release preset emits a PDB; implement the `-netlog` flag and the dump writer (`MiniDumpWriteDump` in an unhandled-exception filter) in the engine during Phase 4.

## Repo layout after bootstrap

The unzipped `zh-pipeline` folder *is* the repo: `bootstrap.ps1` git-inits it, pulls upstream's tree underneath, and keeps this overlay on top
(`CLAUDE.md`, `docs/`, `scripts/`, `launcher/`, `.github/`, `config/`, `assets.sha256`). `.zh-local.json` (gitignored) records the game and build paths.
Agent entry points: `CLAUDE.md`, then `docs/AGENT-TODO.md`.

## Licensing hygiene

Only the compiled exe, DLLs and launcher go in release zips. Never commit or package `.big` files, `Data/`, or anything from the Steam folder. The GPL covers the code; the assets are still EA's, and each of you supplies your own legitimately.
