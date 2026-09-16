# Next Task: Sprite Horizontal-Zoom Lookup Table

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
cleanly, rebuild for the next `DREAMCAST_START_LEVEL`, repeat — no physical
controller input needed between routes.

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
