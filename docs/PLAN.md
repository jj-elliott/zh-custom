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

Target feel: StarCraft 2. The A-star pathfinder stays; the work is in the layer between "where" (pathfinder) and "how" (`Locomotor`). Everything here runs inside the sim, so it must be deterministic: fixed-point or controlled float, verified by the CRC half of the regression harness. Ordered so each step is useful on its own and the cheap ones come first.

1. **Yield rule (~50 lines, no new maths).** An idle unit that is blocking a moving unit steps aside instead of making the mover wait. Lives in the AI idle state. Kills the single most common stall.
2. **Push priority.** Rank contenders when two units collide: moving beats idle, explicit player order beats auto-move (harvester returns, guard), then heavier `Mass` from the INI beats lighter. Lower priority yields; ties go to whoever arrived first. Prevents mutual-yield deadlock and lets an Overlord clear a path through Red Guard rather than wait behind them.
3. **Group pass-through + stuck fail-safe.** Units moving in the same direction ignore collision with each other (SC2's "flow like liquid through a ramp"). A unit stuck past ~2 s temporarily drops allied collision for itself only, replacing the existing teleport-nudge. Never a global collision toggle.
4. **Steering / local avoidance layer.** ORCA-style velocity adjustment per tick, inserted between the pathfinder's next waypoint and the `Locomotor`. Neighbour query from the existing partition manager. Re-request the route if steering pushes a unit more than a cell or two off path. Infantry first; it's the easy case.
5. **Vehicle constraints.** Run avoidance on a circular stand-in, then feed its velocity to the real locomotor as a *target* clamped by turn rate and acceleration, so tanks turn toward the heading rather than slide. Reverse rule: if the desired direction is >~120° behind and the distance is short, back up. Optionally cheat like SC2 and let vehicles rotate in place faster than physics allows.
6. **Flow fields for large groups** (optional). One field per shared destination instead of per-unit searches, plus a formation or crowd target so 50 units don't converge on a single point.

**Status 2026-11-16 — steps 1–3 implemented on `move/collision-rules`, not yet play-tested.** All three are gated on one integer, the
*movement rules level* (`Core/.../GameLogic/MoveRules.h`): 0 retail, 1 = step 1, 2 = +step 2, 3 = +step 3. The exe defaults to the newest level;
`-moverules <n>` overrides it for replay verification and for bisecting a feel problem to a step. It is sim state: both machines must match.

What retail already had (found while reading `AIUpdateInterface::processCollision` / `blockedBy`, so the steps are smaller than planned):
infantry in front of a vehicle is already told to step aside (`aiMoveAwayFromUnit` → `AI_MOVE_OUT_OF_THE_WAY`, a 10 s temporary state);
`Pathfinder::moveAllies` already clears idle allies off a path the pathfinder marked blocked-by-ally; `hasHigherPathPriority` already breaks
deadlocks (dozer > vehicle > infantry > whoever is in front > ObjectID); a stuck unit already gets a 2 s collision holiday in a few corner cases.
The dated feel comes from everything else: a mover that touches an idle ally flags itself stuck and paths around it, and a column queues behind
its slowest member.

- **Step 1 as built:** in `processCollision`, a mover blocked by an idle ally asks it to step aside (same `aiMoveAwayFromUnit` retail uses), once
  per contact; if the ally has nowhere to go the retail stuck-and-repath logic takes over next frame. Not asked: enemies, units that are
  attacking / guarding / busy / using an ability / deployed / disabled, vehicles by infantry, and anything when the mover is within 8 pathfind
  cells of its own goal (so a group arriving doesn't shove its first arrivals off their spots — the tunable to revisit after play-testing).
- **Step 2 as built:** `hasHigherPathPriority` keeps dozer-first and vehicle-over-infantry, then ranks player-ordered move > any other move >
  standing still (`getPushRank`), then heavier `Mass`; exact ties fall to the retail in-front rule. "Arrived first" from the plan is that rule.
- **Step 3 as built:** `blockedBy` returns false for allies both moving within 60° of the same heading (they overlap while flowing and separate
  at their own goal cells). Fail-safe: 2 s of unbroken blocking by allies (own counter; repaths reset `m_blockedFrames`) → collision off for that
  unit for 2 s via the existing `m_ignoreCollisionsUntil`. Deviation from the plan: that timer ignores enemy units too for those 2 s, because
  making it allies-only would change retail uses of the timer (transport exits). The retail repath-when-blocked stays alongside it.
- **Verification so far:** `skirmish-01.rep` still reproduces at level 0 (the gates leak nothing). At levels 1 and 2 the same replay also
  reproduces, i.e. that 6-minute game never hits those rules; at level 3 it diverges at frame 1600 as intended, identically on two runs. So
  the new rules have had almost no automated exercise. **Needed: a played skirmish at the default level** (then `verify.ps1 -Import -Name x`
  makes it the first Phase 7 baseline and the real determinism check), and a verdict on feel per step.

**Play-test 2026-11-16 (level 3, solo skirmish, now `tests/replays/move-01.rep`, reproduces at `-moverules 3`):** flow is much better and
parked groups get out of the way, but free overlap is wrong for this game: whole tank columns stack through a gap, so terrain and choke
points stop mattering, and stacked tanks look odd. Wanted: a choke that is a meaningful slow-down without choking the pathfinding, and
units that end up overlapping should spread apart once they stop, as in StarCraft.

- **Step 3, second cut (level 4, the default; level 3 is kept only so `move-01.rep` stays a baseline for steps 1–2):**
  - *Queue instead of overlap.* A same-heading allied pair in contact is still never "blocked" (no stuck flag, no repath, no stopped
    animation, no shoving the leader), but exactly one of the two, the one behind along the shared heading, caps its speed to the
    other's speed along its own heading (`followInFlow`): ×0.9 when the leader is dead ahead so the gap reopens, ×0.5 when it is
    alongside so two lanes zip together at a gap. A gap therefore passes units at the rate they fit.
  - *Stalled queue.* Each unit keeps `m_flowFrame`, the last frame it or anything ahead of it in its queue moved (copied back from
    its leader, one unit per frame). 3 s old = the head is stuck for some other reason = the pair goes back to the retail blocked
    logic (repath, then the 2 s fail-safe). A queue whose head keeps moving waits indefinitely.
  - *Spread out.* Two idle allies more than half inside each other: the one with lower push priority walks to the nearest free spot
    (`adjustToPossibleDestination`). Retail only did this within half a cell of the same point.
  - Vehicles behind same-heading infantry are excluded: the retail "infantry, step aside" is better than crawling behind them.
  - Not yet play-tested. Level 4 replays `move-01.rep` to frame 2200 before diverging from the level 3 recording, identically twice.
    Open questions for the next session: is the queue speed right (0.9 / 0.5), does the zipper look natural, does spread-out fire
    when expected (it depends on idle units still receiving collision callbacks), does a long queue ever wrongly count as stalled.

**Second play-test 2026-11-16 (level 4, `tests/replays/move-02.rep`, reproduces at `-moverules 4`):** "much better, still a bit too
much overlap". **But level 4 as played never queue-followed:** `m_flowFrame` started at 0, so from frame 90 every unit counted as stalled
and `followInFlow` was never reached (found because `move-02.rep` replayed identically with the first level 5 change, which only touched
`followInFlow`). What was actually played and liked: retail blocking between same-heading units, plus steps 1–2, the 2 s jam fail-safe
(the source of the remaining overlap) and spread-out. Worth remembering if level 5 feels worse: that combination is a known-good fallback.

- **Step 3, third cut (level 5, the default; level 4 keeps the inert behaviour so `move-02.rep` stays a baseline):**
  - *Fix:* a queue episode starts its stall clock at first contact with a flow partner (`m_flowContactFrame`; a 1 s gap = new episode)
    and the unit's own movement refreshes it even while stalled. Queue-following now really runs.
  - *Tight spacing:* the follower's cap is leader speed × room, where room falls from 1 at first touch to 0 at 15% penetration
    (measured against the centre distance at which two boxes with these headings first touch), ×0.5 when the leader is alongside;
    the follower's velocity is cut to the cap immediately (`scrubVelocity2D`) instead of braking down to it.
  - With following working, a moving queue no longer feeds the jam fail-safe, so ghosting should now only happen in a genuinely
    stalled queue (3 s without movement ahead, then 2 s blocked).
  - Not play-tested. Level 5 diverges from the level 4 recording of `move-02.rep` at frame 3600, identically on two runs.
    Watch for: followers halting too abruptly (the immediate velocity cut), stop-and-go pulsing in a queue, 15% being too
    strict or too loose, single-file squeezing where formation neighbours merely brush sides.

**Third play-test 2026-11-16 (level 5, `tests/replays/move-03.rep`, reproduces at `-moverules 5`):** fine around the base, three problems.
(1) An attack-move column jammed behind its front rank when that stopped to shoot a Patriot: the rear waited instead of spreading around
the target to its own firing spots, which is the StarCraft behaviour wanted. (2) In open ground (top right of the map, end of the game)
units crawled and stuttered in column although there was room. (3) The halting was visibly stop-and-go on vehicles. Target given: about
75–80% of the way from level 3 (free overlap) to level 5 (strict), and more fluid.

- **Step 3, fourth cut (level 6, the default; level 5 kept for `move-03.rep`):**
  - *Eased cap.* Light contact costs nothing. Behind a leader the cap eases in from first touch to 30% penetration, beside one only
    from 20% to 50%; at full depth the follower goes 0.8× (0.6× beside) the leader's speed so the overlap backs out. No instant
    velocity cut: the locomotor brakes to the cap, and after contact ends the cap climbs back 8% a frame instead of vanishing.
  - *Stalled queue → path around.* Patience is 1 s (was 3 s), and a stalled follower is flagged blocked-and-stuck, i.e. the retail
    repath with units as obstacles, once a second while the queue stays put. In a real choke that repath finds nothing and the unit
    keeps its place.
  - *Stopped shooters are obstacles.* An ally in an attack state and standing still is not a flow partner and no longer counts as
    "moving" in `processCollision`, so the unit behind is flagged stuck at once and paths around it (`isStoppedToAttack`).
  - Not play-tested. Level 6 diverges from the level 5 recording of `move-03.rep` at frame 3600, identically on two runs.
  - Honest limit: this makes blocked units go *around* stopped shooters toward their move goal; they open fire when that brings
    them into range. It does not make them pick a firing position around the target. If the encirclement still doesn't emerge,
    that is attack-state work (choose an approach point on the range circle that isn't occupied), a separate item.

**Fourth play-test 2026-11-16 (level 6, a campaign mission, so no replay: campaign games are not recorded):** stop-and-go much better,
but units got stuck between buildings, a whole group queued behind one slow damaged unit instead of flowing around it, tracked
vehicles were the worst, and the flow is still not SC2's. Verdict: tuning contact rules is whack-a-mole, because they only act once
units touch and can only slow, wait or repath. Decision: build step 4.

- **Step 4 as built (level 7, the default): `AISteering`** (`GeneralsMD/.../GameLogic/AISteering.h/.cpp`), called from
  `AIUpdateInterface::doLocomotor` between the path follower's aim point and the `Locomotor`. Not ORCA proper but a sampled
  velocity-obstacle scheme, chosen because it is short, has no solver to get wrong, and is deterministic by inspection:
  - Neighbours: non-infantry ground units with AI and physics within reach of a 1.5 s horizon, nearest 24; not structures (the
    pathfinder's job), not what we can crush, not our goal or ignored obstacle, not moving units behind us (they avoid us).
  - If carrying straight on at full speed hits nothing within 1.5 s, nothing happens (the common case, one partition query).
  - Otherwise score: path direction turned 0/±15/±30/±45/±60/±90° (tracked: up to ±45°) at full or half speed, or stop.
    Cost = collision urgency (10 × (1 − t/horizon)²) + straying (0.4 … 3.5, ×1.5 for tracked) + speed given up (1.5 half, 3 stop)
    − 0.3 for the side we already turned to. Right is listed before left, so a head-on pair both go right.
  - A standing neighbour is always scored at full speed: slowing never gets past something that isn't moving, and stopping short
    would keep the contact rules (yield request, path around a shooter) from ever firing.
  - A turned direction must be `isLinePassable` for two cells plus our radius, so steering never leaves the walkable area.
  - Output is only this frame's aim point (same distance, new direction) and speed.
  - Off for: the last three cells before the goal, aircraft taxiing, projectiles, wanderers, ultra-accurate locomotors, production
    exits, docking, entering, and while collisions are deliberately ignored.
- **Step 5, first part (level 7):** tracked vehicles (`moveTowardsPositionTreads`) kept losing all speed at 45° off their aim point
  and pivoting on the spot; now 60°, so they keep rolling through the course changes steering asks for. Reverse rule and
  rotate-in-place cheat not done.
- Steps 1–3 stay underneath as the contact-level safety net (yield, priority, eased queue cap, path-around, jam fail-safe, spread-out).
- **Verification:** all four baselines reproduce at their own levels. New `-replaysoak` engine flag (headless replay keeps running
  after the first CRC mismatch and prints the final logic CRC): every baseline soaked at level 7 runs its whole game with no crash,
  and two runs give the same final CRC each time (skirmish-01 9CF3F214, move-01 D25EC9E2, move-02 B57D12E4, move-03 930C1B3B).
  Steering engages early: level 7 diverges from every recording between frames 1500 and 2300. Not play-tested.
- **Known unknowns for the play-test:** weights are first guesses; tracked vehicles may still be clumsy (only the stop angle
  changed); vehicles ignore infantry entirely; "stuck between buildings" is unexplained and may or may not be touched by this
  (candidates: jam fail-safe or a path-around leaving a unit in a gap) — needs a skirmish replay of it to look at.

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
