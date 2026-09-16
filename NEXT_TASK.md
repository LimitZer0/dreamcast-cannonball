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
