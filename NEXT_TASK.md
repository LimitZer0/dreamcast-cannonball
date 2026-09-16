# Next Task: Sprite Horizontal-Zoom Lookup Table

The native-320x240 stage-1 rerun is documented in
`docs/route-matrix-results.md`, with raw output at
`logs/route-matrix/route-A-A-native320-rerun.log`. It exited cleanly; future
matrix cells must use `tee` when launching `kos-tool`.

15 of 15 `STAGE_LOOKUP` bytes have now been attempted. There are 13 clean
captures: `0x00`, `0x09`, `0x08`, `0x12`, `0x11`, `0x10`, `0x1B`, `0x19`,
`0x18`, `0x24`, `0x22`, `0x21`, and `0x20`. Hardware crashes occurred at
`0x1A` and `0x23`;
their raw logs and stack locations are recorded in
`docs/route-matrix-results.md`. The missing `0x08` stage-2 branch was then
captured cleanly at `logs/route-matrix/stage-0x08-native320.log`.

**This stage-byte batch is a different, narrower thing than the "record a
complete run for every route" methodology described elsewhere in this
file** (see "Data collection: route-coverage matrix" below) — don't treat it
as having satisfied that task. Each of these 15 attempts is a short (~60s,
2 profiling-window) sample starting *fresh* at one isolated
`DREAMCAST_START_LEVEL` byte via a cold boot, not a continuous playthrough
of an entire 5-stage route from its actual start to its ending. None of them
intentionally drive through a fork or an ending as captured content (the two
crashes were unplanned kernel panics, not deliberate crash-sequence
captures), and there is no whole-route or whole-game cumulative histogram
spanning any of these runs — each capture only has its own since-boot
cumulative counter over its ~60s lifetime. Useful as a coarse baseline
("which hzoom values show up right at the start of each stage"), but the
full-route, fork-to-ending capture methodology below is still an open,
separate task.

**Update: partial progress on full-route capture — not resolved yet, three
separate blockers found and two fixed.**

`DREAMCAST_FORCE_FORK` (`oattractai.cpp`) and `DREAMCAST_EXIT_ON_STAGE_ADVANCE`
(`main.cpp`) — see harness doc — let a capture deterministically choose
left/right at each fork and stop exactly when the next stage begins.
Hardware-verified clean in both directions for a *single* isolated stage
(no off-road drift, correct `oroad.stage_lookup_off`). Attempting an actual
continuous multi-stage full-route capture with these then hit two more,
separate blockers:

1. **A genuine crash, fixed.** `Render::supports_vsync()` (the class
   Dreamcast actually uses) didn't override `RenderBase`'s default
   (`false`), even though `rendersurface.cpp` already requests
   `SDL_RENDERER_PRESENTVSYNC`. That made `main_loop()`'s `bool vsync`
   always evaluate false on Dreamcast, so the app ran its own **redundant**
   `SDL_Delay`-based frame-pacing on every single frame, on top of hardware
   vsync. This is the likely trigger for a sustained-runtime crash
   (`Data address error`, kernel panic) that hit twice in a row at
   different points (~9 min via `DREAMCASTAUD_WaitDevice`/`SDL_RunAudio`,
   ~12-13 min via `thd_sleep`/`genwait_wait`/`main_loop`) — both generic
   KOS primitives hammered every frame for the session's lifetime, not
   anything content-specific. Fixed by adding `bool supports_vsync() {
   return true; }` to `Render` (`rendersurface.hpp`). Hardware-verified: a
   full retry ran 16+ minutes clean, past both previous failure points,
   with `spriteperf` data essentially identical sample-for-sample to the
   pre-fix run (confirms the fix doesn't change gameplay/CPU-side
   performance data, only removes the redundant delay) — **the existing
   14-stage-byte baseline in `docs/route-matrix-results.md` does not need
   to be redone over this fix.** Only `renderperf`/`drawperf` shifted
   slightly (`update` ~3ms -> ~5-7ms), because the vsync-wait moved from an
   unmeasured post-present delay into the measured `SDL_RenderPresent()`
   call itself — a measurement-attribution change, not a real workload
   change.
2. **`GS_ATTRACT` has a bounded demo timer, not fixed (design limitation,
   not a bug).** `decrement_timers()` (`outrun.cpp`) resets the whole
   attract demo back to stage 1 after a fixed duration, regardless of
   whether a fork was ever reached — confirmed on hardware: a from-cold-
   boot attract session never completed even stage 1 before recycling.
   This means `GS_ATTRACT` alone can never be used for a full multi-stage
   route capture, independent of the crash above.
3. **Real gameplay works but the AI may not reliably finish a stage in
   time, unresolved.** `globals.hpp`'s `FORCE_AI` (wired to a new
   `DREAMCAST_FORCE_AI` cmake option) keeps `OAttractAI::tick_ai_enhanced()`
   driving during real credited gameplay (`GS_INGAME`), which has no demo
   timeout. Paired with a new `DREAMCAST_SKIP_CREDITS` option (bypasses
   `check_freeplay_start()`/`OMusic::check_start()`'s credit/Start gate,
   same mechanism reverted earlier this session for the opposite reason).
   Hardware-verified this combination genuinely reaches `GS_INGAME` and the
   AI keeps driving with no crash and no attract-timeout — but the one test
   run collected a large amount of `spritezoom` data (877,996 rows in one
   bucket alone) without ever crossing the first fork (`"stage advanced"`
   never fired), suggesting the AI ran out of real gameplay's in-game clock
   before covering stage 1's distance — plausibly from repeated scenery
   collisions (`oattractai.cpp`'s own header comment admits it's tuned to
   tolerate collisions for attract-mode demo purposes, not necessarily to
   reliably clear real gameplay's tighter time budget). Only tried once;
   could be one unlucky run rather than systematic — not yet determined.

**Update: blocker #3 retried, not reproduced.** Same
`DREAMCAST_FORCE_AI`+`DREAMCAST_SKIP_CREDITS` build, now paired with
`DREAMCAST_EXIT_ON_STAGE_ADVANCE`, rerun on hardware
(`logs/route-matrix/forceai-skipcredits-retry.log`). This time the AI
cleared stage 1 well within the in-game clock: `stage advanced 0 -> 1,
stage_lookup_off=9`, clean `arch_exit()`, `Program returned 0`, console back
at the dcload prompt with no crash. Two `spritezoom` profiling windows were
captured en route (~1.52M cumulative rows). This is 1-for-2 on FORCE_AI
reaching the fork — the earlier stall (877,996 rows in one bucket, never
crossing) looks like an unlucky run (probably a scenery-collision streak
eating the clock) rather than a systematic failure, but it's not yet proven
reliable across repeated attempts. `stage_lookup_off=9` is the first
confirmed, non-assumed fork identity captured for this route's first hop —
worth carrying forward as a real data point instead of the "assumed from
start byte" labels used elsewhere in this file.

**Update: two-stage continuous capture, stage 2 fork reached.** Added a
`DREAMCAST_EXIT_AFTER_N_ADVANCES` cmake option (`main.cpp`'s
`DREAMCAST_EXIT_ON_STAGE_ADVANCE` block now counts advances instead of
exiting on the first one) so a single continuous run can let earlier forks
pass and stop at a later one. With `DREAMCAST_EXIT_AFTER_N_ADVANCES=2` on
the same `FORCE_AI`+`SKIP_CREDITS` build, one continuous hardware run
crossed *two* consecutive forks cleanly:
`logs/route-matrix/forceai-skipcredits-stage2fork.log` —
`stage advanced 0 -> 1, stage_lookup_off=9` then
`stage advanced 1 -> 2, stage_lookup_off=18`, then clean `arch_exit()`,
`Program returned 0`. Stage 1's fork identity (`0x09`) matches the prior
single-fork retry, so the AI's route choice looks deterministic run-to-run
so far (still only n=2 confirmations). This is the first continuous,
multi-stage capture with verified (not assumed) fork identities: this
route's stage1->`0x09`, stage2->`0x12`, both of which are also among the 15
isolated-byte matrix entries above -- so those two matrix cells can now be
tied to a specific position in a real route instead of just "a stage-lookup
byte that boots cleanly." Visually confirmed on the TV during this run: the
car went **left** at fork 1 and **left** at fork 2, then reached the
stage-2 checkpoint sign right before the capture exited -- matching
`stage_lookup_off=9` then `=18` in the log, i.e. this is a real,
human-observed **A->A->A**-direction route segment, not just a log-inferred
one.

**Update: three-stage continuous capture, stage 3 fork reached.** Same
build with `DREAMCAST_EXIT_AFTER_N_ADVANCES=3`
(`logs/route-matrix/forceai-skipcredits-stage3fork.log`): three consecutive
forks crossed in one continuous run, no crash --
`stage advanced 0 -> 1, stage_lookup_off=9` ->
`stage advanced 1 -> 2, stage_lookup_off=18` ->
`stage advanced 2 -> 3, stage_lookup_off=26`, `advance 3/3`, clean
`arch_exit()`, `Program returned 0`. Visually confirmed left/left at forks 1
and 2 (see above); fork 3's direction wasn't separately confirmed this run.
Notably `stage_lookup_off=26` is `0x1A` -- one of the two isolated-byte
matrix cells that previously crashed hardware with a `Data address error`
in `genwait_wait`/`SDL_Delay` (see the `0x1A` entry in
`docs/route-matrix-results.md`). It passed clean here as part of a
continuous route, consistent with that crash having been the vsync-fix bug
(`Render::supports_vsync()`, see above) rather than anything specific to
`0x1A`'s stage content.

**Update: full 5-stage route completed end-to-end, hardware-clean.**
Repeated the same build/method with `DREAMCAST_EXIT_AFTER_N_ADVANCES=4` then
`=5`
(`logs/route-matrix/forceai-skipcredits-stage4fork.log`,
`logs/route-matrix/forceai-skipcredits-stage5fork.log`). Full chain of
`stage_lookup_off` values for this continuous run:

| Advance | `stage_lookup_off` |
|---|---|
| 0->1 | 9 (`0x09`) |
| 1->2 | 18 (`0x12`) |
| 2->3 | 26 (`0x1A`) |
| 3->4 | 35 (`0x23`) |
| 4->0 | 0 (game finished, looped back to stage-1 state) |

Notably `0x1A` and `0x23` are the two isolated-byte matrix cells that
previously crashed hardware (`docs/route-matrix-results.md`) -- both passed
clean here as part of a real continuous route, confirming those crashes
were the vsync bug (fixed earlier this session), not anything specific to
those bytes' stage content. Between the stage-4 and final advance the log
shows `[VMU] Saved 20 score entries to CANNON (624 bytes)` -- the run
genuinely reached and completed the ending sequence, not a timeout/reset.
`Program returned 0`, no crash anywhere across all five stages.
Human-observed sequence matched the log exactly: stage 5 ending, the
route map screen (showing which fork was taken at each stage), then the
hiscore entry screen -- which required physical initials input (`FORCE_AI`/
`SKIP_CREDITS` only cover the credit gate and driving, not the name-entry
screen), then a clean exit. So this route isn't fully hands-off
end-to-end yet -- a future *unattended* full-route capture would need
either a forced/skipped hiscore-entry path or `DREAMCAST_EXIT_ON_STAGE_ADVANCE`
tuned to fire before reaching that screen. Route identity visually
confirmed by direct observation (corrected after a re-check against the
footage): the `stage4fork` run showed **left, left, right** at the three
live forks captured in that run (ending stages 1-3), and the full
`stage5fork` run showed **left, left, right, left** at all four live forks
feeding the 5 stages (stage 5 itself ends at the goal, not another split).
This is a real, human-and-log-verified route through the entire game
(**L,L,R,L** through the live forks, corresponding to
`stage_lookup_off` `9, 18, 26, 35` at the four fork transitions and `0` at
the finish/reset), not an assumed A-E label. This resolves the
long-standing "route identity: not assigned" / "full-route capture ...
unresolved" open item below for at least this one route -- the other 4
routes (A->B, A->C, A->D, A->E) still need the same treatment.

**Update: both the cur_stage/stage_lookup_off mismatch and the
hiscore-entry hang fixed and hardware-verified.**

1. **Bug found and fixed:** `OInitEngine::init()` (`oinitengine.cpp`)
   unconditionally set `ostats.cur_stage = 0` regardless of the injected
   `DREAMCAST_START_LEVEL` byte, while `oroad.stage_lookup_off` was set to
   the actual byte. Real (non-`MODE_CONT`) stage advancement
   (`init_split_next_level()`) increments both in lockstep
   (`ostats.cur_stage++`, `stage_lookup_off += 8`) and `check_stage()`
   decides road-split-vs-ending purely from `cur_stage` (`<= 3` -> another
   split, else -> `init_bonus()`/ending) -- so a non-zero start level with
   `cur_stage` stuck at 0 caused a full mismatched 5-stage runthrough
   instead of the intended single-stage test, which is what produced the
   apparent hang (see below). Fixed by deriving
   `ostats.cur_stage = level >> 3` (stage bands are contiguous 8-byte
   groups: `0x00-07`=stage1 ... `0x20-27`=stage5). Hardware-verified with
   `DREAMCAST_START_LEVEL=0x23`: reached the stage-5 ending/route-map
   directly after driving only one stage, instead of playing through five.
   Note the "STAGE 1" HUD digit at boot is unaffected and expected --
   `HUD_ONE` is a hardcoded boot-time tile (`ohud.cpp`'s `draw_main_hud()`),
   only replaced by the real `cur_stage`-derived digit on the *next*
   checkpoint draw, so it doesn't reflect `DREAMCAST_START_LEVEL` and isn't
   a bug indicator by itself.
2. **The earlier "hang" was this bug, not the new hiscore-skip patch.**
   The first `DREAMCAST_SKIP_HISCORE_ENTRY` test (before the `cur_stage`
   fix) froze with the last frame static and audio still running, and its
   log showed no `[VMU] Saved` trace at all -- meaning
   `check_name_entry()`'s skip branch was never reached; whatever it hung
   in was upstream of hiscore entry, consistent with the stage-mismatch bug
   above corrupting engine state during a bad 5-stage runthrough. Required
   a manual controller-chord exit and full console reboot back to dcload.
3. **`DREAMCAST_SKIP_HISCORE_ENTRY` (new, `ohiscore.cpp`) verified clean**
   once the above was fixed: hit the hiscore screen, no wait for
   controller input, `[VMU] Saved 20 score entries to CANNON (624 bytes)`
   logged, no hang, no crash. It then correctly looped back through
   `GS_REINIT`/`GS_INIT` into another `DREAMCAST_START_LEVEL=0x23` run
   (expected -- `DREAMCAST_EXIT_ON_STAGE_ADVANCE` was intentionally off for
   this end-to-end test, so nothing told it to stop; killing the transfer
   manually ended it cleanly). Combining this with
   `DREAMCAST_EXIT_ON_STAGE_ADVANCE`/`DREAMCAST_EXIT_AFTER_N_ADVANCES` for
   a real unattended single-stage-ending capture is untested but should
   now work now that both blockers are cleared.

Raw logs: `logs/route-matrix/hiscore-skip-test.log` (pre-fix hang),
`logs/route-matrix/hiscore-skip-test-2.log` (post-fix, clean).

**Update: fully unattended, self-terminating stage-5-ending capture now
works.** `check_stage()` never touches `ostats.cur_stage` for the stage-5
ending (it goes to `init_bonus()`, not another split), so
`DREAMCAST_EXIT_ON_STAGE_ADVANCE` can never fire there -- a capture would
loop forever through `GS_REINIT`/`GS_INIT` with no exit condition, needing
either the `/pc/exit_now` sentinel or a manual controller-chord exit.
**Important: never wrap `kos-tool` with a shell `timeout` to work around
this** -- killing the host-side tool does not reset/signal the console, so
the game binary keeps running on real hardware with nothing listening, and
the console gets stuck (a later `kos-tool -x` exec attempt will fail with
`EXEC failed after N attempts`) until manually power-cycled.

Added `DREAMCAST_EXIT_ON_GAME_COMPLETE` (`main.cpp`): `ostats.game_completed`
is set the instant the bonus/ending sequence starts and only clears back to
0 in the *next* `OInitEngine::init()` (i.e. once the whole bonus+hiscore
cycle finishes and reinit has run) -- so a 1->0 edge on that flag means a
full ending cycle just completed. Hardware-verified clean with
`DREAMCAST_START_LEVEL=0x23`+`DREAMCAST_SKIP_HISCORE_ENTRY`:
`logs/route-matrix/exit-on-game-complete.log` shows
`[VMU] Saved 20 score entries to CANNON`, then
`ending cycle completed, calling arch_exit()`, then `Program returned 0` --
no sentinel, no manual exit needed. Also notable: the last two runs of this
same combination produced byte-identical score/time, consistent with
`outils::reset_random_seed()` re-seeding identically on every
`Outrun::boot()` -- `DREAMCAST_FORCE_AI` capture runs are deterministic
run-to-run given the same build/settings, which is good for trusting future
before/after A/B comparisons (e.g. the LUT optimization).

The "route identity: not assigned" caveat throughout
`docs/route-matrix-results.md` still cannot be resolved until item 3 above
is sorted out — a full continuous capture needs the AI to reliably survive
long enough to reach an actual ending. Existing captures in that file don't
have verified route identity and would need redoing once a reliable
full-route method exists.

**Important:** the fork decision only applies to the active AI path.
`config.engine.new_attract=1` (this project's default) selects
`OAttractAI::tick_ai_enhanced()`, which picks its route randomly per stage
via `sprite_ai_x` — `DREAMCAST_FORCE_FORK` overrides that pick directly.
Don't try to force the fork via `oinitengine.cpp`'s `car_x_pos`/
`route_selected` check instead — that was tried first and caused a
hardware-confirmed visual bug (car drives off-road because its steering
target and the loaded road data disagree). See the harness doc's
`DREAMCAST_FORCE_FORK` section for the full explanation.

The next stage-2 candidate (`DREAMCAST_START_LEVEL=0x09`) is also captured at
`logs/route-matrix/stage-2-0x09-native320.log`; it exited cleanly, but its
route identity is intentionally unassigned until fork choices are verified.

The unattended batches completed `0x12`, `0x11`, `0x10`, `0x1B`, `0x19`,
`0x18`, `0x24`, `0x22`, `0x21`, and `0x20` cleanly. The `0x1A` cell crashed
in `genwait_wait`/`SDL_Delay`, and `0x23` crashed in
`DREAMCASTAUD_WaitDevice`/`SDL_RunAudio`; both required dcload recovery and
remain documented as failed hardware cells.

Continues `DREAMCAST_OPTIMIZATION_NOTES.md` next step #3 ("Consider
lookup/table or span-based rendering for common hzoom values to avoid
per-output-pixel xacc loops"). A separate session already started a baseline
checkpoint against this repo for this work; this file tracks what's left.

## Prerequisite: cumulative histogram in the profiler

`DREAMCAST_SPRITE_ZOOM_PROFILE`'s `spritezoom` report
(`src/main/hwvideo/hwsprites.cpp`) resets its histogram every
`DC_SPRITE_PERF_INTERVAL_MS` (30s) window. It already tracks a running
`cumulative`/`cumulative-rank` total per hzoom bucket internally — check
whether that's sufficient before adding a second tracking mechanism, or
whether it needs to be exposed/printed on demand (e.g. at the ending/
game-over screen) rather than only alongside each 30s snapshot, so a full
run's whole-game frequency data doesn't have to be combined by hand from
many snapshot lines.

## Data collection: route-coverage matrix

OutRun's branching course has 5 route choices (stage bytes documented under
`DREAMCAST_START_LEVEL` in `docs/dreamcast-test-harness.md`). For full LUT
coverage, record a complete `spritezoom` capture for every route (A→A, A→B,
A→C, A→D, A→E), including attract mode, crashes, traffic-heavy sections,
forks, and endings — not just a couple of 30s samples. Use identical
graphics settings (resolution, widescreen, scale, hires) across every run,
since `hzoom` bucket distribution shifts with render config. Repeat on real
hardware after emulator testing.

`DREAMCAST_AUTOSTART` (this session's addition, see harness doc) makes this
collection unattended: it skips Cannonball's frontend "PLAY GAME" menu and
lets Outrun's own `GS_ATTRACT` AI-driving loop run indefinitely with zero
input, which combined with `DREAMCAST_START_LEVEL` can target a specific
stage's traffic/road pattern directly instead of always starting at stage 1.

The `/pc/exit_now` sentinel-file remote-exit hook (`main.cpp`'s `tick()`,
see harness doc) makes the full A→A...A→E matrix hands-off end-to-end:
launch, collect, `touch build-dc/cd/exit_now` to end that route's run
cleanly, wait for `Program returned 0` / the dcload prompt, remove
`build-dc/cd/exit_now`, rebuild for the next `DREAMCAST_START_LEVEL`, and
upload again — no physical controller input needed between routes. The
sentinel must be removed before the next upload or that run exits immediately.

### First A→A capture (`docs/route-matrix-results.md`) — rerun complete

A durable stage-1/`DREAMCAST_START_LEVEL=0` rerun now exists at
`logs/route-matrix/route-A-A-rerun.log` and is summarized in
`docs/route-matrix-results.md`. It exited cleanly through the sentinel path.
The run still should not be labeled definitively "A→A" until the fork path is
verified:

1. **The "A→A" label is not yet verified.** Which fork gets taken
   at each stage split depends on `car_x_pos > 0` at that checkpoint
   (`oinitengine.cpp:627-663`) — i.e. which side of the road the car is
   steered to, driven by `oattractai.cpp`'s AI logic during attract mode,
   not a hardcoded route index. Nothing in the existing capture confirms
   which fork the AI actually took at each split (would need cross-
   referencing `ostats.cur_stage`/state transitions against the log
   timeline). "A→A" there is an assumed label from "started at
   `DREAMCAST_START_LEVEL=0`," not something confirmed from the run's
   actual path through the branch tree. Before trusting any route label in
   the matrix, confirm the AI's fork choices are actually deterministic
   run-to-run (same build, same settings, same route every time) — if
   they're not, the whole A-E labeling scheme needs a different way to
   pin down which route was taken, not just which stage it started at.

The rerun records the graphics settings in `docs/route-matrix-results.md`:
native 320x240, widescreen `0`, hires `0`, scale `2`, FPS mode `2`, and
vsync `1`.
`DREAMCAST_FAST_SPRITES` was `ON`; this is harmless for hzoom-histogram
purposes since the profiler counts `hzoom` values regardless of which draw
path renders them, but it is not a "before" capture for the LUT-optimization
comparison.

There's a known intermittent crash-on-exit (see harness doc's
`DREAMCAST_AUTOSTART` section) that has at least once required a full
physical reboot + manual dc-load reload from the GD-emu menu — a real cost
if it fires mid-matrix-collection. The sentinel-file exit path has tested
clean so far (preferred over the controller chord/menu Exit for unattended
runs), but the crash isn't fully root-caused — worth a heads-up check
between routes rather than assuming every run in the matrix completed.

## Rendering backend correction

`DREAMCAST_OPTIMIZATION_NOTES.md` (pre-existing, predates this session)
frames the render backend as "SDL2/GLdc" and credits GLdc's texture upload
path as already fast. That's stale: the project has since moved to a custom
SDL2 PVR hardware-rendering driver (`find_pvr_renderer()` in
`src/main/sdl2/rendersurface.cpp`, selecting the SDL renderer named
`"Dreamcast PVR"` — visible in every hardware log this session as
`renderer=0x... name=Dreamcast PVR`). `libGL.a` is still linked (it's in
`SDL2::SDL2-static`'s `INTERFACE_LINK_LIBRARIES` from when the SDL2 port
supported a GLdc backend) but is not the active render path — don't assume
GLdc-specific numbers/behavior in the old notes still apply; re-verify
against the actual PVR driver in `rendersurface.cpp` instead.

## Toolchain caveat

The existing historical numbers in `DREAMCAST_OPTIMIZATION_NOTES.md`
(~19-20 fps, ~20-24ms sprite time, ~3,400 rows/frame in heavy sections) and
in the separate baseline-checkpoint effort were captured against
`sh-elf-g++ 15.2.0`. This session's toolchain is `sh-elf-g++ 17.0.0`
(confirmed via `sh-elf-g++ -print-search-dirs`) — re-measure before trusting
those figures as still current; a toolchain upgrade already caused one real
build break this session (`libGL.a` addon symlink went missing after an
update, see harness doc's Build section).

## Once the LUT/span-based approach is implemented

Guard it behind a `DREAMCAST`-specific CMake option (matching
`DREAMCAST_FAST_SPRITES`'s pattern), keep it easy to A/B against the
existing general zoom loop, and validate on real hardware before removing
the old path — per this project's general patch rules (smallest safe
change, hardware decides).
