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

## Phase 2 — pipeline (done 2026-10-21; releases are attended via `scripts/release.ps1`, `release.yml` is the manual fallback)

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

## Phase 3 — dad's machine (done 2026-10-21: kit = unzip + double-click; first two-machine session played. Tailscale needs Direct Connect, see AGENT-TODO #13; hitches observed → Phase 4)

1. Copy his Steam Zero Hour folder to e.g. `C:\Games\ZeroHour-Dev` (Steam's copy stays untouched as a fallback).
   **It must be vanilla:** no GenTool (`d3d8.dll` crashes rebuilt exes), no GenPatcher (changes `.big`/INI content → desync against a vanilla
   peer). If he ever installed them, run Steam's "Verify integrity" and delete the leftovers (`d3d8.*`, `GenTool*`, numbered `NNN_*.big`,
   `CustomContentMaps.big`, `EdgeScroller.exe`, `launcher\`). The launcher's `assets.sha256` check will refuse to start otherwise.
2. Drop `ZHLauncher.exe` and `launcher.json` into that folder.
3. In `launcher.json` set `Repo` to `jj-elliott/zh-custom` and leave `Token` empty: the repo is **public** (decided 2026-10-21), so the
   launcher needs no credentials, nothing expires, and GitHub Actions is free. Set `MapPack` to the current map-pack tag.
4. Shortcut to `ZHLauncher.exe` on the desktop. Done. From now on he just double-clicks it.

Launcher behaviour: compares `BUILD_VERSION.txt` to `releases/latest`; downloads and overwrites if different, keeps `generalszh.exe.prev` as a one-step rollback; if offline it launches whatever is installed.

Because you both always run the tagged exe, every setting you bake into code is identical on both ends, so lockstep config drift can't happen.

## Phase 3b — parity with the GenTool/GenPatcher setup (see `docs/FEATURE-PARITY.md`)

The game played on 2026-10-20 was already upstream's engine (weekly-2026-10-02) under GenTool 7.6 with GenPatcher add-ons. Engine
features carry over; the rest is: Control Bar Pro/HD and other add-on `.big`s shipped deliberately in the release zip with
`-forcefullviewport`, a camera height/pitch scale enforced in network games, map-pack sync in the launcher, and shared overlay/replay/FPS
defaults. GenTool itself stays out (crashes rebuilt exes; its unique features are small to add). AGENT-TODO #9–#11.

## Phase 4 — netcode (first feature)

Read first: `GeneralsMD/Code/GameEngine/Source/GameNetwork/` (FrameData, NetworkInterface, ConnectionManager, FrameDataManager). The wiki's `crcdiff` page is the desync tooling.

Context: StarCraft II, AoE4, Factorio and Stormgate are all still deterministic lockstep. Nobody replaced lockstep for RTS; they changed what happens *around* it. The sequence below copies those techniques in order of value-per-effort.

0. **Transport first (no code).** Hamachi adds a relay hop and jitter. Swap to Tailscale (WireGuard, direct peer-to-peer) or a plain port-forward before measuring anything.
1. **Instrument, don't guess.** Log: negotiated frame delay (run-ahead), per-frame stall time waiting on the peer, RTT *and* jitter, and the CRC mismatch frame + which subsystem's CRC diverged. Play one real session and read the log.
   **Done 2026-10-21 (v0.2.0):** `-netlog`, `Core/GameEngine/.../NetLog.cpp`, on by default on both machines; the launcher shows the last log's path. Next: read a real session log.
   **First real log, 2026-10-21 (`tests/netlogs/2026-10-21-session1-*.log`, `scripts/netlog-summary.py`):** 11 minutes, 40 stalls on each
   side, **11% of the game frozen**. 37 of 40 stalls last 1.7–2.0 s; median gap 12 s. Round trip 90–120 ms, jitter 4–33 ms, run-ahead
   settles at 5–7 frames (≈200 ms) — all healthy. Retries: 61 of 5741 packets on one side (≈1%), 102 of 2490 on the other (≈4%).
   **Every long stall on the peer coincides with a retry on the host** (40/40, frame-aligned). Cause: `Connection::m_retryTime` is
   hard-coded to **2000 ms** (`Connection.cpp`), so a single lost command packet freezes both players for ~2 s. Jitter is not the problem.
2. ~~Jitter-aware run-ahead.~~ **Adaptive retry (replaces this step, same slot).** Resend an unacked command after ≈1.25× the measured round
   trip (floor ~60 ms, cap ~500 ms, mild backoff) instead of 2 s, and use ~200 ms before any sample exists. With run-ahead ≈200 ms a
   one-RTT resend lands inside the buffer, so a lost packet costs no visible stall at all. Bandwidth is ~25 packets/s, so extra resends
   are free. Transport-level only: duplicates are already dropped by command ID (upstream fix #3564); the simulation is untouched.
   Validate with the solo loopback rig at 2–5% injected loss, then one real session. Jitter-aware run-ahead stays as a later option if
   logs ever show stalls with latency spikes rather than retries.
   **Done 2026-10-22 (`Tests/netlogs/2026-10-22-solo-loss4-*`, rig at 4% loss + 20 ms jitter, 3.5 min):** 3 stalls total, two of them in
   the first 3 s while the run-ahead settles from 30 to 4, one of 147 ms mid-game; frozen time 0.6% (host) / 0.1% (peer) vs 11% before,
   longest stall 810 ms (startup) vs 2.0 s. Retry timer sat at the 80 ms floor on loopback. Shipped in v0.3.0; next real session confirms.
3. **State resync instead of termination (Factorio model).** On CRC mismatch: host serialises game state (the save-game path already exists), ships it, guest loads and rejoins at that frame. ~2-second hitch, no more dead games. Host is the source of truth; this is already how Factorio handles desync.
4. **Local input acknowledgment (the SC2 trick).** Respond to the player's input *immediately* on the UI layer: selection highlight, unit voice bark, order marker, start of turn animation. The actual command still executes N ticks later in lockstep. This is why SC2 feels responsive at the same real latency, and ZH currently gives zero feedback until the command clears the wait. Cheap, pure presentation layer, biggest *felt* improvement.
5. **Catch-up instead of stall (Factorio).** When a client falls behind, run the sim at 2–3× until caught up rather than freezing. With the decoupled render loop from Phase 5, a hitch becomes a brief fast-forward.

Explicitly skipped: GGPO-style rollback. Needs per-tick snapshot/restore of the full sim; ZH has no cheap state copy. Full server-authoritative rewrite stays a stretch goal; steps 3–5 get most of the benefit.

## Phase 5 — rendering (near term)

Reality check: every ZH fork is x86 (inline asm) and D3D8 fixed-function. No fork has D3D11. Visual wins come from the layers *around* the D3D8 path, not from replacing it yet.

1. **DXVK (zero engine code).** `release.yml` now bundles DXVK's `d3d8.dll` + `d3d9.dll` (native D3D8 support, no d3d8to9 shim) and a `dxvk.conf` forcing 16× AF and swapchain MSAA. Modern Vulkan path on both machines, pinned version, ships in the zip. Requires a Vulkan-capable GPU on your dad's side; check.
2. **Decouple render from the 30 Hz sim.** ~~If upstream hasn't, port Reforged's commits.~~ **Already upstream (checked 2026-10-21):**
   `Core/GameEngine/Source/Common/FramePacer.cpp` owns the render clock; `GameEngine::canUpdateRegularGameLogic` steps logic from a
   time accumulator (xezon, 2025-08-06); `FramePacer::getLogicFramePhase()` gives the sub-frame fraction, and `Drawable::applyPhysicsXform`
   (bobtista, 2026-09-14) plus `ParticleSys.cpp` lerp with it. Uncapped FPS is `FramesPerSecondLimit=0` / `-fps` / `RenderFpsPreset::UncappedFpsValue`.
   What is *not* interpolated yet: the drawable's base position/orientation (only the locomotor bob/pitch/roll and particles). Catch-up mode
   (Phase 4 step 5) can build on `setLogicTimeScaleFps`. Skip the port; just verify smoothness in-game and extend interpolation if needed.
3. **Textures.** AI-upscale the `.dds` in the `.big` archives (ESRGAN on the 5090), re-export with mipmaps into a higher-priority `.big`. Reforged ships 481 originals at 4× as reference. ~~Set the exe LAA flag~~ LAA set 2026-10-22. **Scoped down 2026-10-22:** the UI
   chrome is not upscaled any more; it is redrawn (step 3b). An upscale pipeline for world textures exists parked on `render/textures`
   (`scripts/make-texture-pack.ps1`); the UI atlases are no longer a target.
3b. **Menu UI system (`render/ui`, design signed off 2026-10-22).** `docs/UI-DESIGN.md` is the spec. Procedural chrome drawn through new
   `Display` primitives, a style module (`GameClient/UIStyle`), per-gadget renderers, 16:9-native layout descriptions, a new typeface.
   **Done 2026-10-24** for the shell: steps 2 to 5b of the doc's §7 (primitives, style module, typeface, main menu, skirmish and LAN
   setup, map selection, direct connect, LAN lobby, options, load/save, replays, message boxes), all signed off from the owner's own
   play; fonts and layouts ship in the game zip, `-classicui` kept for one release. Not on it yet: Generals' Challenge, credits and the
   score screen (art-led), and everything in game (pause menu, save/load and replay popups, the HUD): its own later pass.
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

**Current plan: the section "Direction change: the StarCraft 2 model" below. Everything between here and there is history.**

Target feel: StarCraft 2. The A-star pathfinder stays; the work is in the layer between "where" (pathfinder) and "how" (`Locomotor`). Everything here runs inside the sim, so it must be deterministic: fixed-point or controlled float, verified by the CRC half of the regression harness. Ordered so each step is useful on its own and the cheap ones come first.

1. **Yield rule (~50 lines, no new maths).** An idle unit that is blocking a moving unit steps aside instead of making the mover wait. Lives in the AI idle state. Kills the single most common stall.
2. **Push priority.** Rank contenders when two units collide: moving beats idle, explicit player order beats auto-move (harvester returns, guard), then heavier `Mass` from the INI beats lighter. Lower priority yields; ties go to whoever arrived first. Prevents mutual-yield deadlock and lets an Overlord clear a path through Red Guard rather than wait behind them.
3. **Group pass-through + stuck fail-safe.** Units moving in the same direction ignore collision with each other (SC2's "flow like liquid through a ramp"). A unit stuck past ~2 s temporarily drops allied collision for itself only, replacing the existing teleport-nudge. Never a global collision toggle.
4. **Steering / local avoidance layer.** ORCA-style velocity adjustment per tick, inserted between the pathfinder's next waypoint and the `Locomotor`. Neighbour query from the existing partition manager. Re-request the route if steering pushes a unit more than a cell or two off path. Infantry first; it's the easy case.
5. **Vehicle constraints.** Run avoidance on a circular stand-in, then feed its velocity to the real locomotor as a *target* clamped by turn rate and acceleration, so tanks turn toward the heading rather than slide. Reverse rule: if the desired direction is >~120° behind and the distance is short, back up. Optionally cheat like SC2 and let vehicles rotate in place faster than physics allows.
6. **Flow fields for large groups** (optional). One field per shared destination instead of per-unit searches, plus a formation or crowd target so 50 units don't converge on a single point.

**Status 2026-11-16: the six steps above were the plan; this is what exists.** One evening, nine play-tested iterations on
`move/collision-rules`, ending in a rebuild. Read this instead of the step list.

### Current design: `MoveRules::UNIFIED` (level 1, the default; `-moverules 0` = retail)

Three parts, each doing one job, and everything they replace switched off:

| part | job | where |
|---|---|---|
| **Traffic map** (`MoveTraffic`) | *where*: the pathfinder knows the routes other units have planned | `Core/.../GameLogic/MoveTraffic.h/.cpp`; hooks in `AIPathfind.cpp` (`examineNeighboringCells`, `examineCellsCallback`, `prependCells`, `newMap`), `AIUpdateInterface::computePath` |
| **Steering** (`AISteering`) | *how*: get past units over the next few metres; queue; escape a jam | `GeneralsMD/.../GameLogic/AISteering.h/.cpp`; called from `AIUpdateInterface::doLocomotor` |
| **Contact rules** | only what the other two can't: idle ally steps aside, stacked idle units spread out, push priority, 2 s escape when blocked by standing allies | `AIUpdate.cpp` (`processCollision`, `blockedBy`, `hasHigherPathPriority`, `shouldAskToYield`, `isSteeredPair`) |

- **Traffic map.** When a ground unit gets a path, every cell under it (unit width) is stamped with the frame the unit expects
  to be there and a load count. The A* adds 2 per other unit (max 2; a cell costs 10) for cells stamped within 5 s of our own
  estimated arrival; the half-price "beeline to goal" shortcut pays the same; and at most one path node in eight running alongside
  traffic is pinned (`setCanOptimize(false)`) so the line-of-sight smoother can't cut back through it. Stamps lapse by time, nothing
  is erased. Arrival time = straight-line cells from the search start × frames per cell at top speed.
- **Group moves** no longer use the retail shared ground path with 2 (vehicles) or 3/5 (infantry) columns
  (`AIGroup::groupMoveToPosition`): each unit paths for itself to its own offset destination and the traffic map spreads them.
  Explicit formations still use the retail formation move.
- **Steering.** Sampled velocity obstacles: if carrying on hits a unit within 1.5 s, score the path direction turned
  0/±15/±30/±45/±60 (infantry also ±90) at full or half speed, or stop; cost = collision urgency + straying + speed lost. A chosen side
  is kept 1 s; the turn eases in and out; a mild far-off threat doesn't start a manoeuvre; turned directions must be passable.
  *Queueing:* a moving unit in front that we already touch sets our pace. *Escape:* held at a standstill by that for 2 s → this
  unit ignores collisions for 2 s. Infantry ignore infantry; nothing avoids what it can crush; buildings are the pathfinder's.
- **Between two moving allies there are no contact rules at all** (`isSteeredPair`): no blocked/stuck/repath, no shove, no
  deadlock-breaking. Retail's remain for enemies, standing units, and movement steering doesn't handle (taxiing aircraft,
  production exits, docking, entering).
- **Tracked vehicles drive while turning** (`Locomotor::moveTowardsPositionTreads`): speed factor 1 → 0.2 from 0° to 90° off the
  aim point, → 0 at 110°, pivot beyond. Retail stopped to pivot at 45°.
- All tuning constants are at the top of `AISteering.cpp` and `MoveTraffic.cpp`.

### Verification, and what it does not show

- `tests/replays/skirmish-01.rep` reproduces at `-moverules 0`: the switch leaks nothing into retail behaviour.
- `scripts/soak.ps1`: the five recordings from the earlier iterations (`tests/soak/`, inputs only, no longer baselines) plus
  skirmish-01, each run twice under UNIFIED with `-replaysoak`: all run to the end, no crash, identical final CRCs.
- The user decided (2026-11-16) not to keep intermediate iterations reproducible: determinism yes, per-iteration baselines no.
  **A UNIFIED replay is only expected to reproduce on the build that recorded it** until the design settles; then record baselines.
- Not shown by any of this: how it plays. The traffic map and the steering-owned queueing have never been played. Soak replays
  diverge early and their recorded orders then mostly miss, so late-game crowding is barely exercised.

### How we got here (what each play-test taught; the code for these is gone)

1. *Idle allies yield, push priority, free pass-through for same-heading allies.* Flow great; overlap erased choke points.
2. *Queue instead of overlap, spread-out.* Liked, but a stale timer meant queueing never ran: what was liked was retail
   blocking + 2 s escape. Found only because the replay reproduced under a change that should have altered it.
3. *Queueing fixed, strict spacing.* Too strict, stop-and-go; attack-move columns jammed behind units that stopped to shoot.
4. *Eased cap, path around stalled queues and stopped shooters.* Better, but "whack-a-mole": contact rules act too late.
5. *Local avoidance added on top.* Flowed better; jitter, a spinning unit, mixed infantry/vehicle groups bad.
6. *Sticky, eased steering; vehicles avoid infantry.* Jitter and spins gone; corners still clunky, groups collide.
7. *Lanes for vehicle group moves* (never played). Found retail forces groups into two columns on one path.
8. User's call: the layers fight each other; build one congestion-aware system and delete the rest. → the design above.

Lessons worth keeping: a contact rule can't produce flow; every extra layer that also touches speed or path is a source of
twitching; a reproducing replay is evidence only if the change under test could have altered it; campaign games and games
loaded from a save record no replay.

### Play-tests of UNIFIED

- **First (2026-11-16, `tests/soak/move-06.rep`, 4:22; reproduced on the build that recorded it, the first real-play determinism
  check):** "decent for a first pass". (1) Units over-turn, the back-and-forth jitter on turning is back. (2) Units over-spread,
  some setting off sideways or backwards after an order. Causes found in the first cut and changed: the traffic penalty was up
  to +18 on a 10-cost cell and is now at most +3; stamps were one cell wider than the unit each side, so a group's bands tiled
  its whole frontage, now exactly unit width; the half-price beeline stopped at traffic, which made any cell with a clear
  beeline look cheap even behind the unit, now traffic is priced on the beeline; every third node beside traffic was pinned,
  leaving stair-stepped paths, now at most one in eight; tracked vehicles carried 30% speed to 90 degrees, now 20% and pivot
  from 110. The user's guess (pathfind grid too coarse) was not the cause: the grid is 10 units, about half a tank.
  A test save now exists (`movetest.ps1 -Name pathfinding`).

- **Second (2026-11-16, from the `pathfinding` test save, no replay):** "still a bit sticky, units not flowing enough". No detail on
  where. Changed on the likeliest causes: queueing used the padded avoidance radius and the whole front half-plane, so units
  abreast were permanently paced by whichever neighbour was a nose ahead (now: within 85% of the bare combined radius and 45
  degrees of dead ahead); slowing/stopping cost 1.5/3.0 against going round (now 2.5/5.0); jam escape after 1 s (was 2 s);
  traffic penalty 2 per unit, max +4 (was 1, max +3). If it is still sticky, ask *where*: setting off, mid-route, at a
  choke, or on arrival; each points at a different part.

- **Third (2026-11-16):** "certainly an improvement". Two design asks from the user, both built:
  - *Infantry.* Making vehicles path around friendly infantry adds load and jitter; do what StarCraft does: the vehicle is not
    slowed at all and the infantry are gently, almost invisibly pushed aside. Built as `AISteering::nudgeAside`: friendly
    infantry inside the strip the hull is about to sweep (hull length + 1.5 cells ahead, hull width + their radius + 1) are slid
    sideways out of the nearer edge at up to 1.5 units a frame, by position only: no AI state on either side. Never onto ground
    they can't walk on (other side tried, else the vehicle passes through). Busy infantry are left alone. A standing man's
    pathfinder cell claim moves with him. Friendly infantry no longer block a vehicle in `blockedBy`, so the retail "stop,
    send the infantry on a get-out-of-the-way errand" never runs for them; vehicles no longer steer around friendly infantry.
  - *Route negotiation.* The user's idea: let every unit take its optimal path, then iteratively shift paths to remove
    overlap, charging by how little a unit loses from a detour, so a Humvee is the one pushed off and a Crusader keeps the
    inside track. Built as the one-pass form of that (prioritized planning): a group order is pathed slowest unit first
    (`AIGroup::groupMoveToPosition` sorts by top speed, then distance), so slow units see an empty traffic map and get the
    direct line; and what traffic costs a unit scales with its top speed (0.5× to 1.75×, 1 unit/frame = 1×), so nimble units
    are the ones that go round. Infantry are out of the traffic map entirely. **Not built: the iterative part** (re-pathing
    units after everyone has a route until overlap stops falling). One pass in the right order is what makes iterations
    mostly unnecessary, but two groups ordered separately, or units ordered one at a time, still get first-come-first-served.
    If that shows, the next step is a bounded re-plan: after a group's paths exist, re-path the fastest units whose routes
    still share cells, a fixed number of rounds per frame.

- **Fourth (2026-11-16):** "a better solve". Still some clumping; infantry sometimes oscillating along a vehicle's hull;
  swinging wide works but not reliably. The user asked for a principled way to tune: a harness that measures how a mixed
  group gets through a course. Built: see "Benchmark" below. Infantry oscillation: a nudged man is now put 3 units beyond the
  hull's edge instead of exactly on it (`nudge.overshoot`).

### Benchmark (`scripts/movebench.ps1`, `GameLogic/MoveBench.h`)

A course = a test save + move orders. `generalszh.exe -headless -loadsave <save> -movebench <course|auto>` loads the save
without a window, gives the orders, simulates flat out until every ordered unit is at rest, and prints: meanArrive, maxArrive,
goalDist, overlap (vehicle-pair-seconds >40% inside each other), reversals (turn-direction flips per unit = jitter), stalled
(unit-seconds standing while trying to move), detour. About 2 s a run. Random generators are re-seeded, so a run repeats to the
last digit; `bench.seed=N` gives variant N (other seed, destination moved up to 3 cells) and the script averages over `-Seeds`.
`-movetune name=value,...` overrides any tuning constant for the run (23 of them; `movebench.ps1 -ListTunables`).
`-Record -Course x` launches the game with a recorder that writes the orders a person gives; `auto` needs nobody: every combat
ground unit of the local player to the map centre.

**First results (save `pathfinding`, course `auto`, 27 units, 6 variants; score = meanArrive + 0.5 maxArrive + 2 overlap +
reversals + 0.5 stalled, lower is better):**

| configuration | score | meanArrive | overlap | reversals | stalled |
|---|---|---|---|---|---|
| unified, defaults | 66 | 23.8 s | 2.6 | 15.5 | 5.5 |
| retail (`-moverules 0`, 1 variant) | 2215 | 162 s, timed out | 0.7 | 177 | 3570 |
| traffic cost off | 97 | 24.5 | 13.0 | 22.5 | 7.9 |
| steering avoidance off | 73 | 23.3 | 10.7 | 6.9 | 3.8 |
| steering and traffic off | 89 | 23.7 | 17.2 | 8.2 | 6.2 |
| infantry nudge off | 67 | 23.7 | 2.9 | 15.2 | 5.7 |
| queueing off | 69 | 23.8 | 3.8 | 15.0 | 5.6 |

What that says:
- Retail on this course never finishes: units spend most of three minutes standing while trying to move. Treat the size of
  the gap with care (one course, and the retail group move to a possibly awkward map centre), but the direction is not in doubt.
- The traffic map earns its keep: without it overlap is 5 times higher and jitter 45% higher. Steering cuts overlap 4-fold.
- **Steering is where the jitter comes from:** 15.5 turn flips per unit with it, 6.9 without. That is the price currently
  paid for the lower overlap. Arrival time is the same (23 to 24 s) in every configuration: none of this costs speed.
- **The constants are not the problem.** Ten constants swept over 2 to 4 values each: no setting beats the defaults by more
  than the spread between course variants (10 to 30 points). A single-variant sweep had suggested several "wins" (horizon 30
  scored 53 against 67); with six variants they vanished. Do not tune on one run.
- The nudge and the queueing rule are invisible to these metrics on this course (few infantry in the way, no real choke).

So further gains need a structural change to steering, judged with this harness, not different numbers. Candidates: steer in
continuous velocity space instead of 15-degree steps (ORCA proper); after passing, hold the sideways offset instead of
returning to the path line (the return is a second turn and often a re-encounter); let the path follower aim further ahead.
Missing from the harness: a course with a real choke and one with infantry in the way (record them), reversals per unit
type, and anything about how it looks.

### Scripted scenarios and the suite (`tests/courses/*.scenario`, `scripts/movesuite.ps1`)

The user asked whether courses could be made without a person (map editor, or a save built from scratch), so the agent can
iterate alone. Answer: a scenario is a text file: a stock map, groups of units to spawn (`spawn <template> <count>`), where each
group starts and where it is sent (`from` / `to`, a waypoint name such as `Player_1_Start` or coordinates). `-movebench
<x.scenario>` loads the map as an empty single-player game, spawns the units on free ground around the start, gives the orders
and measures. No map editor, no save, no keyboard; a new course is a few lines. Format in `MoveBench.cpp`.

`movesuite.ps1` runs every scenario for the defaults, any `-Configs`, and retail, and prints one averaged line each.
**This is the loop for movement work now: change, `movesuite.ps1`, compare.** A person is needed only to judge how it looks.

Four scenarios so far: `desert-cross` and `river-cross` (two vehicle groups swap bases and meet head-on), `desert-mixed` and
`city-mixed` (one group with infantry across open desert / through a town).

**Baseline, 2026-11-16, 3 variants per scenario, mean over the four:**

| configuration | score | meanArrive | overlap | reversals | stalled |
|---|---|---|---|---|---|
| unified, defaults | 241 | 65.2 s | 45.4 | 28.9 | 15.0 |
| retail | 323 | 81.2 s | 5.8 | 7.8 | 331 |
| unified, steering avoidance off | 287 | 65.9 | 75.7 | 7.9 | 23.6 |
| unified, traffic cost off | 302 | 66.6 | 62.0 | 48.1 | 22.4 |
| unified, wider queueing | 267 | 65.8 | 51.4 | 29.9 | 38.0 |
| unified, wider queueing, no jam escape | 1108 | 96.7 | 148 | 24.9 | 1222 (jams) |

Read honestly: **unified gets groups there 20% sooner and almost never leaves a unit standing, but vehicles overlap 8 times
as much as in retail and change turning direction nearly 4 times as often.** That is the "clumping" and "jitter" the user
keeps reporting, now with numbers. Retail's weakness is the opposite: tidy, but 331 unit-seconds of standing still.
(The earlier single-save result made unified look better than this because that course had no opposing traffic.)

**Targets for the next iterations:** overlap below 10 and reversals below 12 while keeping meanArrive near 65 and stalled
below 30. Pointers from the table: steering is the only thing holding overlap down and the main source of reversals;
making units queue harder just moves time into jams unless something lets a queue drain; with no contact rules between
moving allies (`isSteeredPair`) nothing stops two units that failed to avoid each other from driving through one another.

### First unattended iteration against the suite (2026-11-17)

Method: break each bad number down until it names a cause, fix the cause, re-run. Two diagnostics were added for it:
the suite's breakdown columns (`ovMM` / `ovMS` = overlap with both / one of the pair moving, `ovCross` = pairs from
different groups, `revSteer` = reversals while steering was active, `revEnd` = near the goal, `wobble` = reversals within
half a second of the previous one) and `scripts/movetrace.ps1` (per-unit reversals; `-Unit <id>` traces one unit's
steering decisions and heading frame by frame).

What the breakdowns said, and what was done:

1. **Overlap was not a head-on problem.** 44 of 45 pair-seconds were between units of the *same* group, both moving:
   not on a collision course, simply on the same line, which avoidance cannot see. **Separation** (`sep.*`): an allied
   vehicle under way that is within 90% of the combined radius of another eases itself away, half the penetration
   each, 25% of it a frame, 0.5 units a frame at most, never onto ground it could not drive on. Overlap 45 -> 1, arrival
   unchanged, stalled time down. This is the only place a moving unit's position is set directly.
2. **Fellow travellers are not obstacles** (`steer.sameDirCos`, `steer.sameDirPace`): a unit heading our way at no
   less than 70% of our speed is left to separation and queueing. A single group crossing open desert turned 14 times
   a unit dodging its own neighbours, 2 times without.
3. **The steering choice had no memory.** Traces showed three flickers, each fixed on its own evidence: two candidates
   within 0.05 of each other taken on alternate frames (`steer.stick`: last frame's candidate is 0.6 cheaper); the need
   for a turn dropping out every few frames (`steer.holdFrames`: a turn is held 8 frames after it was last asked for);
   the least sufficient turn hunting 60 > 15 > 30 > 60 degrees within a manoeuvre (`steer.ratchet`: while a manoeuvre
   lasts the turn only grows). Easing 0.25 -> 0.12. A manoeuvre is now out, hold, back.

**Suite after, 5 variants per scenario (retail from the 3-variant run):**

| configuration | score | meanArrive | overlap | reversals | wobble | stalled |
|---|---|---|---|---|---|---|
| unified, before (2026-11-16) | 241 | 65.2 s | 45.4 | 28.9 | - | 15.0 |
| unified, now | 141 | 65.5 s | 1.4 | 18.2 | 7.4 | 9.2 |
| unified, now, avoidance off | 139 | 66.5 s | 3.9 | 8.9 | 2.6 | 9.7 |
| retail | 323 | 81.2 s | 5.8 | 7.8 | 3.2 | 331 |

Per scenario, reversals now: desert-mixed 7, desert-cross 12, city-mixed 26, river-cross 28. **Open ground is at the
target; chokes are not.** Overlap target met (below retail). Not yet met: reversals < 12 overall.

Things learnt that change how the numbers are read:
- The reversal count has a floor that is not ours: a Humvee on a long diagonal weaves under retail rules too (33
  reversals for one unit, steering never active). `wobble` separates that from quick flip-flops.
- With separation in place avoidance is worth little on these courses (overlap 1.4 vs 3.9) and costs half the
  reversals. It is kept because it is what makes opposing groups pass instead of meeting, which the suite measures
  only as `ovCross`; whether it earns its keep is a play-test question.
- Gentler or smaller separation (`sep.max`, `sep.gain`) trades overlap back for nothing.

Not looked at by a person yet: separation slides a vehicle sideways without turning it. The numbers cannot say
whether that reads as "units making room" or as "tanks skating".

### Play-test of that build, and the invariant (2026-11-17, later)

User, on the pathfinding save: "way too much oversteering", "units that have no business continuing to freak out and
shift left and right", four Crusaders swinging wide and blocking Humvees that then "did that back and forth dance";
"we're doing odd parameter tuning in a spot where we're structurally incorrect". Then two principles, in their words:
- **Least action:** "a unit won't deviate from its planned course unless it has a reason to do so."
- **One model:** "are we making decisions based off a shared fundamental model of how unit movement works? This is
  a deterministic system, so there shouldn't be any surprises after we plan."

The morning's suite numbers had improved while the thing the user sees got worse: reversals fell because turns were
held and ratcheted, which is oversteer. **Lesson: a count of reversals does not measure unjustified deviation.** New
suite columns do: `steerPct` and `pushPct`, the share of moving unit-frames in which avoidance bent the aim or cut the
speed, and in which separation moved the unit. Before: 12% and 12% (14% and 3% on the user's save).

The code was then read against the first principle. Five violations, all fixed in `AISteering.cpp` (its header
comment states the invariant and the three permitted reasons: follow, avoid, separate):
1. Collisions were predicted at the unit's top speed, not the speed it was doing. A Humvee keeping pace behind a
   Crusader was "about to ram it" every frame and dodged a collision that was never going to happen. This is the
   back-and-forth dance.
2. A manoeuvre under way continued for any threat however distant (the start threshold dropped to zero).
3. A turn was held 8 frames after its reason had gone, and could only grow (the morning's hold and ratchet). Removed.
4. Separation began at 90% of the combined radius; two tanks abreast are inside that without touching. Now 80%.
5. Queueing was all-or-nothing and only on contact. Replaced by **follow**: a unit ahead, in our lane, going our way
   sets our speed (its pace plus gap / headway). Speed only, never a turn. Fellow travellers are never avoided.

Result (5 variants; save = the user's pathfinding save, 3 variants):

| | steerPct | pushPct | reversals | wobble | overlap | meanArrive |
|---|---|---|---|---|---|---|
| suite, morning build | 11.7 | 12.4 | 18.1 | 7.1 | 1.2 | 65.1 s |
| suite, now | 6.0 | 10.5 | 17.0 | 7.9 | 2.6 | 66.3 s |
| save, morning build | 14.2 | 3.0 | 12.7 | 5.5 | 1.0 | 23.5 s |
| save, now | 4.9 | 9.8 | 9.7 | 3.8 | 2.1 | 23.7 s |

Honest reading: avoidance now fires half to a third as often, and that is all this step bought. Units are still pushed
apart in one frame in ten, which means **the plans themselves put units of one group on the same line** and the local
layers spend the journey repairing that. That is the user's second principle, and it is the next piece of work:

**Next: the plan must already be collision-free for the group it is made for.** Sketch, to be confirmed with the user:
- One route per group move (a corridor), and each unit a lane in it: its lateral offset, kept from where it stood
  relative to the others when the order came, squeezed where the corridor narrows. Units of one group then never
  plan onto each other's line, and need neither avoidance nor separation among themselves.
- The traffic map becomes a reservation in time made with the locomotor's own numbers (acceleration, speed lost in
  turns), not top speed, so that "who is where when" is a prediction the simulation keeps.
- Measure it before trusting it: `planErr`, the difference between the frame a unit was planned to enter a cell and
  the frame it did. If that is small, conflicts can be settled when the plan is made; if not, the model is wrong and
  that is the thing to fix.
- Local avoidance stays only for what a plan cannot know: a new order, the enemy, a unit that stopped to shoot.
  Target: steerPct and pushPct near zero for a single group on open ground.

### Lanes: a group's plan no longer puts its own units on one line (2026-11-17, evening)

Built on the user's go-ahead. `AIGroup::friend_moveLanesToPos` (its comment is the specification):
- **Clusters.** Vehicles linked by gaps of at most 8 cells travel together; a selection of two clumps is two plans.
  (First cut used the whole selection's centroid: on the user's save, two clumps 1000 apart were marched to one route
  between them, arrival 23.7 -> 31.5 s.)
- **One route per cluster** (`findGroundPath` from its centre), and **a lane per unit**: about sqrt(n) lanes, 2 to 6,
  one unit-width plus half a cell apart. A unit's plan is the route's corners moved sideways by its lane, then its
  place in the block at the destination. It follows that with the engine's own follow-path state, as retail's two
  columns did.
- **Slow units in the middle lanes, fast on the outside**, and the cut between lane pairs is moved to where the speed
  changes so a lane holds one kind of unit. This is the user's earlier "Crusader keeps the inside track, the Humvee
  goes round", now decided in the plan.
- **Lanes are fitted into the passable ground at each corner**, probed for the widest unit, slid over as a set or
  closed up. (A route's corners hug obstacles: centring lanes on them collapsed three of four lanes onto one point at
  the river, and a lane point that was valid ground but too pinched to drive to left one unit circling for ever.)
- **Corners near the start and the end are dropped** (the start of a route is how the cluster's middle gets onto it,
  not somewhere every unit should go).
- **Lane-mates merge in row order.** Each unit knows its lane and row (`AIUpdateInterface::setLane`, transient); a
  unit beside a lane-mate with an earlier row drops back instead of both reaching the line together. This is follow
  with knowledge of the plan, and it took separation among a single group from 18% of frames to 6%.

Not lanes: infantry (they pass through each other), attack-move (`groupAttackMoveToPosition` is still per unit),
formations, waypoint-queued moves, clusters of fewer than `MinVehiclesForGroup`.

**Suite (5 variants), and the user's save (3):**

| | steerPct | pushPct | reversals | wobble | overlap | meanArrive | stalled |
|---|---|---|---|---|---|---|---|
| suite, before lanes | 6.0 | 10.5 | 17.0 | 7.9 | 2.6 | 66.3 s | 7.4 |
| suite, lanes | 5.8 | 9.1 | 15.7 | 7.3 | 2.1 | 68.4 s | 7.8 |
| retail | 0 | 0 | 7.8 | 3.2 | 5.8 | 81.2 s | 331 |
| save, before lanes | 4.9 | 9.8 | 9.7 | 3.8 | 2.1 | 23.7 s | 3.8 |
| save, lanes | 4.0 | 5.1 | 8.6 | 3.4 | 0.8 | 23.4 s | 3.1 |

Per scenario, reversals before -> after: desert-mixed 7.9 -> 2.2 (retail 2.3; steerPct 1%, the invariant holds),
city-mixed 24.9 -> 14.7, desert-cross 9.6 -> 12.1, river-cross 29.1 -> 33.7.

Honest reading: **one group moving on its own is now planned, and it shows. Two groups meeting head-on are not
planned against each other at all**, and both head-on scenarios got slightly worse because eight tidy lanes now meet
eight tidy lanes nose to nose. Arrival is 2 s (3%) slower overall; not yet explained (suspects: forming the block at
the end, lane points that must be touched).

**Not built from the proposal:** time reservations with the locomotor's real numbers, and the `planErr` metric. The
old traffic map (top-speed arrival estimates, soft cost) is unchanged and now matters less for a single group; turning
it off costs about 4 reversals a unit, so it stays for now.

**Next, in order:**
1. Two plans against each other. Cheapest honest form: when a cluster's route is planned, look for another cluster's
   lanes coming the other way along the same ground (the traffic map already knows) and give each side one half of
   the width, as traffic does. This is a reason known at plan time, not a keep-right habit applied to everyone.
2. `planErr`, then reservations in time, if 1 is not enough.
3. Attack-move through the same planner.
4. Why arrival is 2 s slower.

### Direction change: the StarCraft 2 model (2026-11-17, night). THIS IS THE PLAN NOW

The user, after the lanes build: "we really should not be re-inventing the wheel here and just try to emulate the
fantastic unit movement of SC2 ... we're getting into the same 'rules-based' whack a mole that we didn't like from the
retail build. The fact that we're using 'at retail levels' like it's a good thing is showing me that we've lost the
plot." And: implement it "figuring out what we need to do to support the best version of it, not rushing to the
finish line."

So `UNIFIED` (level 1: traffic map, `AISteering`'s three reasons, group lanes) is a dead end that stays only as a
comparison until the new system plays better, then is deleted. **Retail is not a yardstick any more**: level 0 exists
to verify old replays. The bar is SC2's feel; the method is SC2's architecture, built in its own order with each
layer checked on its own before the next one stands on it.

**The architecture (the user's brief), and what each part meets in this engine:**

| SC2 | here |
|---|---|
| Walkability grid | Exists: the pathfinder's cells (10 world units; clear / water / cliff / rubble / obstacle / impassable, fence bit). It stays the source of truth for *what the ground is*; structures keep stamping it as they do now. |
| Constrained Delaunay navmesh | **New, M1 done.** One mesh for every kind of unit: every boundary between cell types is a constrained edge and every triangle has a label, so a search filters by the unit's surfaces (ground, water, cliff, rubble, fence for crushers) the way the grid search does. |
| Dynamic obstacle carving | **M2.** Every footprint change already funnels through three places (`internal_classifyObjectFootprint`, `classifyFence`, `classifyMap`; bridges through `changeBridgeState`). A whole 475x475 map rebuilds in 5 ms, so M2 starts with "rebuild when the grid changed, at most once a frame" and only goes to local retriangulation if a measured late-game base says it must. |
| Clearance, layers | **M2, done except wall tops.** A mesh per unit size, built from ground shrunk by that radius (Demyen's per-crossing widths were built and dropped, see below), so a wide unit's mesh simply has no gap it cannot pass. Bridges are the engine's layers 2..14: each gets its own mesh of a window round it, joined to the ground's mesh where their ground meets ("Bridges in the mesh" below). Wall tops (layer 15) still use the grid. |
| A* over triangles + funnel | **M3.** A* on triangle edges with the clearance test, then the funnel with the unit's radius, so the path is the taut string a unit of that width can follow. Replaces `findPath` / `findGroundPath` / `patchPath` for ground units at the new level; aircraft are untouched. First playable milestone (new paths, old follower). |
| Path caching / throttling | **M3.** The engine already queues path requests (5000 cells a frame). A group order searches the triangle corridor once per cluster and funnels per unit. |
| Velocity steering (seek, separation, cohesion), spatial hash | **M4.** Replaces `AISteering`, lanes, the traffic map and the retail blocked/stuck/repath machinery at the new level. Neighbours from `ThePartitionManager` (iteration order is deterministic). |
| Predictive avoidance | **M4.** |
| Push priorities (moving displaces idle; right of way) | **M4.** Today allied units are never pushed apart by physics, they overlap and "blocking" is AI speed limits. The new level gives ground units hard circles: positional separation, with idle units pushed and returning (or not: SC2 leaves them). |
| Dynamic arrival radius | **M5.** Replaces `adjustDestination`'s one-cell-per-unit goal stamping for group moves. |
| Attack-move transition | **M5.** Target acquisition already exists; the work is leaving and re-joining the group's flow. |

**The one place this engine is not SC2, and it decides M4.** SC2 units are circles that turn almost instantly and
accelerate almost instantly, so the velocity steering asks for is the velocity the unit has. Here a Crusader is a
long box that pivots at a limited rate, wheeled units cannot turn at all while stopped, and motion is forces and
friction in `PhysicsBehavior`. Every jitter of the last two days came from that gap: a layer asking for a motion the
locomotor could not deliver, then correcting. M4 therefore has two halves:
- steering produces a *desired velocity* from the navmesh path and the neighbours;
- a single drive model turns it into motion with the unit's own limits (turn rate, acceleration), and steering's
  predictions use that same model (the user's "one model" principle). Ground units stop being force-driven:
  position and heading are integrated directly, as the engine already does for a braking unit.

**Decided by the user (2026-11-17): keep Generals turn rates and acceleration.** And the criterion that goes with
it, in their words: "we're building a system that looks like it's working well / as intended and that any slowness
is deliberate and not a 'bug' ... Smooth, well planned movement that is slow reads much better than movement that
feels like it could be faster if the system was built better. The first feels like a design decision and the second
a bug."

What that means for M3/M4 (the test every steering decision is held to):
- A slow turn is fine; a *correction* is not. A tank turns once, at its rate, onto a line it then keeps. So the path
  a unit is given must already be one it can drive: the funnel's corners become arcs of the unit's own turning
  circle (or a stop-pivot-go where the engine's treads do that), and the plan's timing uses the unit's own
  acceleration. Steering never asks for a velocity the drive model cannot produce next frame.
- Waiting must look like waiting: a unit that cannot go yet stands still facing where it will go, it does not
  creep, wiggle or try alternatives.
- Suite metrics follow from this: turn reversals not explained by the path's own corners, speed changes not
  explained by a neighbour or a corner, and time spent neither moving nor arrived. Arrival time is reported but is
  not the target.

**Milestones. Each ends with its own check; nothing is wired into the sim until M3.**

| | what | done when |
|---|---|---|
| **M1** | Mesh from the grid: boundary chains, simplification, CDT, labels. Standalone test tool. | **Done 2026-11-17**, see below. |
| M2 | Rebuild on grid change (**done**); unit width (**done**: a mesh per size, below); bridges (**done**, below; wall tops not); debug overlay in game. | fuzz covers edits; mesh == grid after every footprint change in a soak run; overlay looks right to the user. |
| M3 | Point location, A*, funnel; `MoveRules` level 2 routes ground paths through it (**done**, below, bridges included); corners as drivable arcs. | path exists <=> grid says a path exists, on every suite map; lengths <= grid paths; plays. |
| M4 | Drive model + steering + hard separation + push priorities; removes level 1's layers at level 2. | suite: no overlap, deviation only with a neighbour as cause; the user plays it against SC2 in their head. |
| M5 | Arrival radius, group goals, attack-move. | same. |
| M6 | Delete level 1 (`AISteering`, lanes, `MoveTraffic`); level 2 becomes the default. | verify + soak; docs. |

**M1 as built** (`Core/.../GameLogic/NavMeshBuild.h/.cpp`, the method is in the header; `NavMesh.h/.cpp` is the
engine side; `tests/navmesh/navtest.cpp` + `scripts/navtest.ps1` test it without the engine):
- Boundaries between cells of different label are traced into chains between junctions, simplified
  (Douglas-Peucker, 1 cell), and any chain whose simplified form touches another goes back to its exact form, so the
  boundary set is always a planar graph and the triangulator never sees crossing constraints.
- Incremental Delaunay (Lawson flips) then constraint insertion by edge flipping (Sloan), region labels by flood
  fill from the two sides of every boundary.
- **Determinism:** integer lattice coordinates and exact 64-bit predicates only; no floating point anywhere in the
  build, loop orders fixed by the grid. The mesh is derived state like the grid: rebuilt after load, not saved, not
  in the CRC.
- Not sim state yet: built only with `-movetune navmesh.build=1` or `navmesh.dump=1`. `skirmish-01.rep` verifies.

| map (scenario) | grid | boundary points | triangles | build | cells on the other side of a simplified boundary |
|---|---|---|---|---|---|
| desert-mixed | 204x270 | 3040 -> 327 | 542 | 1.2 ms | 0.60% |
| river-cross | 320x320 | 7004 -> 877 | 1452 | 2.8 ms | 0.75% |
| city-mixed | 475x475 | 11042 -> 1268 | 2108 | 4.8 ms | 0.62% |

Fuzz: 20000 random grids x 4 tolerances, 0 failures (neighbour symmetry, orientation, area, Delaunay on every
unconstrained edge, labels, and at tolerance 0 the mesh equals the grid cell for cell).

**M2, first part: the mesh follows the grid** (2026-11-17). `NavMesh::markDirty` is called where the pathfinder
changes cell types (`internal_classifyObjectFootprint`, `classifyFence`, `classifyMap`, `changeBridgeState`);
`NavMesh::update`, once a frame before the path queue, rebuilds the whole mesh if a label really changed. Checked on
the seven recorded games with `-movetune navmesh.check=1,navmesh.log=1` (every second the grid is compared with what
the mesh was built from): 14 to 53 rebuilds a game, never stale, every rebuilt mesh passes validation. At a few
milliseconds each that is far below anything worth a partial rebuild.

**Search and unit width, standalone** (2026-11-17, `NavMeshQuery.h/.cpp`, `NavBuild::erode`; not wired into the game):
- **A unit's width is in the mesh, not in the search.** Each size of unit gets a mesh built from ground already
  shrunk by its radius (`erode`: half-cell resolution, exact integer distances, per pass-mask), and on that mesh the
  unit is a point. Consequences: the pulled string is exactly as far from every wall as the unit needs, "is my
  centre on my mesh" is the whole wall test (M4's steering needs nothing else), and the funnel is the plain one.
  Sizes follow the engine's own quantisation (the grid pathfinder already rounds units to diameters of 1..5 cells).
- **What was tried first and thrown away:** one mesh, Demyen's crossing widths, and a funnel that wraps the string
  round a circle at every corner. Reachability was right, but the string was not: when a gap is seen at a slant the
  circles at its two ends swap sides as seen from the apex, and the funnel bends the string round the wrong one (a
  full loop round a corner in the fuzz). The point-shrinking shortcut (move portal ends inward) let paths pass at
  0.04 radii from a wall. Neither is a tuning problem, so the approach went.
- **Search:** A* over (triangle, entry edge). Each node keeps the corridor with the shortest *pulled string* to its
  entry edge (a real length), the open list is ordered by a lower bound, and the search continues past the first
  complete corridor until no open node's bound beats the best string. One corridor per node is a heuristic.
- **Simplification is now safe by construction:** a boundary is never straightened so far that the centre of a cell
  beside it changes side. So at every tolerance the mesh and the grid agree about every cell, and therefore about
  what is connected to what. (Before this rule a 1-cell tolerance cut the city map in two for wide units.) This
  settles the earlier worry about the mesh and the grid disagreeing.

Fuzz (40000 random grids; each also shrunk for radius 0.5 and 1.5 cells; exact and simplified boundaries; ~500000
start/goal pairs against an exhaustive grid search):

| | result |
|---|---|
| path exists on the mesh <=> path exists on the grid | always |
| path leaves its mesh | never |
| wide unit's path nearer a wall than its radius | never (nearest: exactly 1.000 radii) |
| length vs the grid's 8-direction path | mean 0.91 (radius 0), 0.95 (wide); longer than the grid path in 0.03% of searches, worst +12% |

Real maps, simplified boundaries, a path between opposite corners:

| map | radius (cells) | triangles | shrink + build | path search |
|---|---|---|---|---|
| city-mixed (475x475) | 0 / 0.5 / 1.5 | 7926 / 7764 / 4848 | 17-18 ms | 1.0-1.8 ms, 1100-1660 nodes |
| river-cross (320x320) | 0 / 0.5 / 1.5 | 3856 / 3704 / 2556 | 8-9 ms | 0.02-0.04 ms |
| desert-mixed (204x270) | 0 / 0.5 / 1.5 | 1294 / 1218 / 928 | 4 ms | 0.02-0.04 ms |

What this costs and what is still owed:
- A mesh per (pass-mask, size) in use, each 4-18 ms to rebuild when a structure changes the grid. Rebuild lazily
  (on the next search that needs that mesh), never all in one frame.
- The search re-pulls the string for every node (quadratic in corridor length): 1-2 ms across the city map. Fine for
  now; an incremental funnel is the known fix if group orders make it matter.
- `locate` is a linear scan. Needs a bucket grid before the game calls it every frame.
- The 0.03% longer-than-grid paths: acceptable, but they are the kind of thing that reads as a bug when seen. If one
  shows up in play, the fix is to keep more than one corridor per node.

**Wired in: `MoveRules::NAVMESH`, `-moverules 2`** (2026-11-17; the default is still level 1).
- `NavMesh::findPath` (called from `AIUpdateInterface::computePath`): picks the mesh for the unit's surfaces and
  size (radius in half-cells, rounded down; infantry are points), built on first use and rebuilt lazily after the
  grid changed; joins a start or destination that is beside the mesh to the nearest point of it (the destination
  only to connected ground); returns the pulled string as a `Path` whose every node is a corner. Searches are
  charged to the frame's path budget.
- No answer from the mesh (not connected; a unit far from any ground of its size; a unit on a wall top) -> the grid
  search, exactly as before. (Until "Bridges in the mesh" below, that was also every route over a bridge.)
- At level 2 the traffic map and group lanes are off (both exist to repair grid paths). Everything else is still
  level 1: `AISteering` follows the path and keeps units apart. **That is the wrong follower for these paths** (every
  unit of a group gets the same string and they are then pushed apart: pushPct doubles) and it is what M4 replaces.

Suite, 3 variants (level 1 = traffic map + lanes + steering; level 2 = mesh paths + steering):

| | meanArrive | reversals | wobble | stalled | overlap | pushPct | paths from the mesh |
|---|---|---|---|---|---|---|---|
| level 1, all | 68.4 s | 15.9 | 7.5 | 8.3 | 1.7 | 9.1 | |
| level 2, all | 65.4 s | 8.7 | 4.6 | 4.8 | 2.8 | 19.2 | |
| level 2 city-mixed | 82.8 | 8.1 (was 15.3) | 3.6 | 5.6 | 3.7 | 28.5 | all (54 of 54 in one run) |
| level 2 desert-cross | 54.9 | 4.0 (was 12.3) | 1.6 | 2.7 | 3.0 | 19.0 | all |
| level 2 desert-mixed | 61.3 | 2.7 (was 2.3) | 1.4 | 5.0 | 0.6 | 7.3 | all |
| level 2 river-cross | 62.4 | 19.8 (was 33.8) | 11.9 | 6.0 | 3.9 | 21.8 | none: every route needs a bridge (before bridges were meshed, see below) |

Reading: straight strings instead of grid paths halve the turning on their own, and units arrive sooner; the
overlap and pushing are the old follower meeting identical paths. Do not tune that follower.
`soak.ps1 -Level 2`: the seven recordings run to the end twice with equal final CRCs.

**Bridges in the mesh** (2026-11-17/18; `NavBuild::erodeSheet` / `assignSheets`, `NavQuery::Atlas`, `NavMesh.cpp`).
A bridge is a second level over the same piece of map, so one flat mesh cannot hold it.
- **A sheet per bridge.** The pathfinder already says what a unit on a bridge's layer stands on: the layer's cell
  where it has one (deck, or the bridge's sides), the ground cell everywhere else (`Pathfinder::getCell(layer, ..)`).
  That view, over a window round the bridge, is the bridge's sheet; it is shrunk by the unit's radius like the
  ground, so a wide unit keeps clear of the railings and a bridge too narrow for it has no deck and no sheet. The
  ground's own mesh is sheet 0. Only near a bridge's ends do the two views differ about the *ground*: the ground
  sheet sees a wall where the low deck begins (the grid blocks ground under a low bridge, and the meshes keep that),
  the bridge's sheet sees the deck a unit drives onto.
- **Every piece of ground belongs to one sheet** (`assignSheets`): to the bridge whose sheet has it (nearest deck
  wins where two windows overlap), otherwise to the ground sheet. A unit's centre may be wherever *any* sheet allows,
  and that whole area is covered exactly once, so sheets never overlap and a corridor never has to choose between
  two copies of the same ground. Sheets meet along grid lines, which every mesh keeps exactly (`buildMesh`'s
  `exactLabels`), and `Atlas::connect` joins each pair of triangle edges, one from each side, along the stretch
  they share. The search crosses those joins like edges and the funnel pulls one string through all of it: a path
  over a bridge is taut from bank to bank.
- **What was built first and thrown away** (a day of it): sheets that *overlapped* (the bridge's window complete,
  ground included, joined to the ground mesh only along the window's edge). A corridor could then leave a window and
  come straight back on the other sheet, the strip of triangles folded over itself, and the funnel, which works in
  the plane, pulled strings that left the mesh. Clipping portals to the outside of the window fixed validity but
  left detours (sheets could only be changed on the window's edge). Sharing the ground out removes the overlap and
  with it the whole class.
- **Two things the search needed for this:** (1) no corridor passes through a triangle twice: where ground meets
  deck or another sheet there are vertices with open ground all round, and a corridor circling one is pulled
  straight through it; (2) at a bridge's mouth a triangle can have two neighbours across one edge (the deck, and
  ground of another sheet under the deck), so a corridor may come in by that line and turn back across it: the
  strip is unfolded (mirrored in the line, exact in floating point because these are grid lines) before the string
  is pulled, and the point where it touches the line is added. If the search closes every way to a connected goal
  (one corridor per node plus rule 1: seen once in 144,000 searches) a plain corridor's string is used.
- **Engine side.** `NavMesh::readLayers` snapshots each bridge's layer cells with the ground labels (same
  dirty/compare/rebuild path; `changeBridgeState` already marked it dirty). A destroyed bridge's cells are all
  impassable: no deck, no sheet, so it disconnects; repaired, it is back at the next search. `NavMesh::findPath`
  takes the unit's layer and `getLayerForDestination`; at a bridge's end, where a ground-layer unit already stands
  on the bridge's connect cells, it may start on either (`objectInteractsWithBridgeLayer`). Path nodes where the
  string gets on and off the deck, and its corners between, carry the bridge's layer: that is what the follower
  reads (`Path::computePointOnPath` -> `Pathfinder::updateLayer`), exactly as with grid paths.

Fuzz (`navtest.ps1 -Fuzz 9000`: besides the 9000 single-mesh grids, 3000 random maps with 1-3 bridges at eight
angles, low / high / mixed clearance, each built for radius 0 / 0.5 / 1.5 cells, exact and simplified; 144,000
start/goal pairs, half of the goals on a deck) against an exhaustive search over half-cells that knows nothing of
sheets (ground wherever any sheet allows; on and off a deck where that bridge's own view has ground beside it):

| | result |
|---|---|
| path exists in the atlas <=> the exhaustive search finds one | always (100,301 paths, 44,278 of them over a bridge) |
| path leaves its sheet's ground / is on the deck where it says it is not / changes sheet off shared ground | never |
| wide unit's path nearer a wall than its radius | never beyond the half-cell tolerance (nearest 0.943 radii, see below) |
| length vs the exhaustive 8-direction path | mean 0.94; longer in 0.47% of searches, worst +54% |
| searches that needed the fallback corridor | 1 |

Two honest caveats in those rows. *0.943 radii*: a half-cell is ground if its centre is far enough from every wall,
so one corner of it can be nearer by up to 0.086 cell; round a wall such a corner is a dent no string enters, but
where two sheets' ground meets the dent can be the doorway between them. *0.47% longer*: that is the search's
one-corridor-per-node rule (3-in-10,000 on a single mesh), which bites more often where joins give a triangle many
neighbours; searching past the stopping bound changes nothing, so it is not the bound. Keeping more than one
corridor per node is the fix and triples the search's cost: do it together with sharing one corridor search across
a group order, not before.

In the engine (headless, `movetrace.ps1 -Scenario river-cross -Level 2`, Winding River, 4 bridges, 32 tanks):

| | before | after |
|---|---|---|
| paths from the mesh / over a bridge / not connected (grid fallback) | 0 / 0 / 32 | 32 / 32 / 0 |
| all units arrive (timeout) | yes (0) | yes (0) |
| the tanks' set of meshes (radius 1 cell) | 3558 triangles | 5 sheets, 4142 triangles (584 in the four bridge sheets), 90 joins; 307 search nodes a path |
| every bridge down before the order (`-Tune bench.bridgesDown=0`) | | 0 from the mesh, 32 "not connected", no sheets: nobody crosses |
| ... up again at frame 450, order repeated at 500 | | rebuilt once, 32 over a bridge, all arrive |
| order repeated at frame 900 with 9 units on a bridge (`bench.reorder=900`) | | all re-pathed by the mesh (68 paths, 0 without a start), all arrive |

Suite at level 2, 3 variants (the other three scenarios have no bridge on their routes and are unchanged to the last digit):

| | meanArrive | reversals | wobble | stalled | overlap | pushPct |
|---|---|---|---|---|---|---|
| river-cross before (grid paths) | 62.44 | 19.82 | 11.88 | 6.02 | 3.93 | 21.83 |
| river-cross after (mesh paths) | 62.25 | 13.46 | 6.88 | 8.71 | 2.69 | 28.83 |
| all four, before | 65.36 | 8.66 | 4.62 | 4.84 | 2.79 | 19.16 |
| all four, after | 65.31 | 7.06 | 3.36 | 5.51 | 2.48 | 20.92 |

Same reading as before: straighter paths, a third less turning; stalled and pushPct up because 16 tanks now share
one string across a bridge and the old follower pushes them apart. That is M4's job, not a reason to touch the mesh.
`verify.ps1` (retail replay, level 0) passes; `soak.ps1 -Level 2` passes on the seven recordings.

Still owed on bridges: wall tops (layer 15; units on them use the grid); two bridges that touch each other (the
deck of one cannot open onto ground the other owns; no stock map does this); `locate` / `nearestPassable` are still
linear scans, now over every sheet near the point.

**Next:** an in-game overlay (mesh + a unit's path) so the user can look at it; a bucket grid for `locate` /
`nearestPassable`; more than one corridor per node together with one corridor search per group order; then M4,
starting with the string's corners turned into arcs a unit can drive (the user's criterion above).

### Open (level 1; superseded by the section above)

- Play-test UNIFIED (test saves: `scripts/movetest.ps1`). Expected trouble spots: traffic penalty too weak or too strong
  (units still sharing a corner, or taking silly detours), pinned nodes making paths stair-step, infantry clumps overlapping
  while moving, tanks swinging wide into buildings, units arriving through each other near the goal.
- "Stuck between buildings" (4th play-test) was never explained.
- Units don't choose a firing position around a target; they only path around units that stopped to shoot.
- Step 6 flow fields: not needed if the traffic map does its job.

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
