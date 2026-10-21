# Agent TODO — deferred until running inside the repo

These need the actual source tree or a Windows machine, so bootstrap.ps1 left them to you. Work top-down; tick off here.

- [ ] **1. Write `docs/CODE-MAP.md`.** ~20 entries, one line each, path + what it does: pathfinder, `Locomotor`, AI idle/move states,
      `Drawable` / logic-frame boundary, `DX8Wrapper` and `DX8Renderer`, main loop / frame pacing, network layer (frame delay / run-ahead,
      packet handling, CRC check and the "mismatch" disconnect), save-game serialisation, partition manager, INI parsing for unit `Mass`/kind flags,
      replay record/playback, crash handling. This is what every later session reads first.
- [ ] **2. Make `scripts/verify.ps1` real.** Find upstream's headless/replay-test mechanism (wiki: `replay_testing`, `crcdiff`; search source for
      headless, `-replay`, CRC logging flags). Implement the invocation, make sure a CRC-per-frame log lands at the path the script expects,
      record one baseline from a short skirmish replay in `tests/replays/`. Add it as a second job in `.github/workflows/release.yml`.
- [ ] **2b. Smoke-test `scripts/run.ps1`** with `-TimeoutSec 30 -Screenshot 20 -Args "-win"`; confirm it harvests the game's own log
      (find where the engine writes its debug log; add that path to the harvest list) and that the screenshot shows the main menu.
- [ ] **3. Confirm the Release build emits a PDB** (`generalszh.pdb` next to the exe). If not, add `/Zi` + `/DEBUG` for Release in the preset or CMake.
- [ ] **4. Check whether upstream already decoupled render from the 30 Hz sim** (changelog, search for interpolation / frame-rate uncap).
      Record the finding in `docs/PLAN.md` Phase 5 step 2.
- [ ] **5. Crash dump writer.** Unhandled-exception filter calling `MiniDumpWriteDump` to `crashdumps\<timestamp>.dmp` in the game dir.
      The launcher already looks there.
- [ ] **6. `-netlog <path>` flag** writing the Phase 4 instrumentation (run-ahead, stall ms, RTT, jitter, CRC mismatch frame). Launcher passes it when `Debug: true`.
- [ ] **7. `/LARGEADDRESSAWARE`** on the exe if not already set (needed before the texture pack).
- [ ] **8. First feature:** Phase 4 step 2 (jitter-aware run-ahead), behind verify.ps1.

Done items: move them down here with the commit hash.
