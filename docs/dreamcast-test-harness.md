# Dreamcast Hardware Test Harness (kos-tool, perf logging, VMU)

Canonical reference for running Cannonball on real Dreamcast hardware — perf
profiling (`spriteperf`/`videoperf`/`drawperf`/`renderperf`), VMU save/load
checks, and A/B comparisons before/after an optimization. Read this before
re-deriving the console IP, launch pattern, or config/VMU gotchas from
scratch each session.

## Quick reference

- **Console IP: `192.168.0.128`**
- Ping it first: `ping -c 2 -W 2 192.168.0.128`. No response after 2 tries
  means the console is off/rebooting — don't launch, wait or ask the user.
- Before every launch: `ps aux | grep -i "kos-tool\|sh-elf-gdb" | grep -v grep`
  — must be empty. A leftover process means a previous session's console
  state is unresolved; do not launch on top of it.
- Cannonball is **not** a parking/differential build — it's a normal game
  loop. No `-g`, no GDB, ever. It only stops when you exit it (menu Exit, or
  a physical/chord reset) or it crashes.

## Build

```bash
source /opt/toolchains/dc/kos/environ.sh
cd build-dc
./dreamcast.sh   # kos-cmake -> make -> cannonball.elf
./dc.sh          # cannonball.elf -> cannonball.bin -> 1st_read.bin -> cannonball.cdi
```

`dc.sh` runs `sh-elf-objcopy`, `scramble`, then `mkdcdisc -n DCCannonball -d cd/
-e cannonball.elf -o cannonball.cdi -N`. Only needed if you're burning a disc
or testing the CDI path specifically — `kos-tool` below uploads the ELF
directly and doesn't need a CDI rebuild for a normal edit/test loop.

## The three `config.xml` copies — know which one you're editing

There are three files with this name in the tree and they are **not**
interchangeable. Confirm which one a given test path actually reads before
editing:

| Path | Used by |
|---|---|
| `build-dc/cd/config.xml` | Baked into `cannonball.cdi` via `mkdcdisc -d cd/`. What a real disc/CDI boot reads as `/cd/config.xml`. |
| `build-dc/cd/res/config.xml` | Unrelated staged copy under `cd/res/` — not read by `Config::load()` at all. Easy to edit by mistake since it sits right next to the one that matters. |
| `build-dc/config.xml` | What `kos-tool`'s dc-load session actually falls back to. See below — this is the one that matters for a live `kos-tool` test session. |

`Config::load()` (`src/main/frontend/config.cpp`) always tries
`/cd/config.xml` first. Under `kos-tool -m cd/`, `dc-load`'s `/cd/` mount does
**not** reliably serve that file (`Config::load failed to read
/cd/config.xml` is expected and normal here — not a bug), so it falls back to
`/pc/config.xml`, which is dc-load's passthrough to the host directory you
launched `kos-tool` from — i.e. `build-dc/config.xml`. **For a `kos-tool` dev
session, edit `build-dc/config.xml`, not `build-dc/cd/config.xml`.** For an
actual CD-R/CDI boot, edit `build-dc/cd/config.xml` instead — that path
reads `/cd/config.xml` directly with no fallback needed.

Relevant settings for A/B perf testing:
- `<layout_debug>` (`engine` section): `0` (default) boots straight to
  `GS_INIT` -> `GS_ATTRACT` (AI-driven demo). `1` skips attract mode
  entirely and boots into `GS_INIT_GAME` (manual driving / LayOut debug
  overlay) — the opposite of a repeatable AI-driven benchmark. Leave at `0`
  for attract-mode profiling.
- `<new_attract>` (`engine` section): `1` enables Cannonball's enhanced
  attract mode (multiple camera viewpoints, improved AI) vs. the stock
  single-view arcade demo loop.

A saved VMU config (see below) overrides whatever's in the XML on next boot
if `CANNON` exists and is valid — wipe the VMU save if you need a guaranteed
fresh-from-XML boot.

## VMU save/load

- **On-disk filename is `CANNON`** (6 chars, `/vmu/[port]1/CANNON`), not
  "cannonball" — that's only the `app_id` metadata shown in file managers.
  Don't go looking for a file literally named `cannonball` on the VMU.
- `Config::load()` always calls `vmu_load_config()` at boot, which silently
  falls back to XML defaults if no valid `CANNON` exists — it does **not**
  create a file just from booting.
- `Config::save()` (`src/main/frontend/menu.cpp:462/509/527`) is the only
  thing that writes `CANNON` — it fires from the in-game **Options/Settings
  menu**, not from the course-select/play-game menu and not automatically on
  boot or on plain navigation. Reaching the play-game menu proves nothing
  about VMU write behavior; you have to actually enter Options.
- Expected boot log on a successful load:
  ```
  [VMU] Open file /vmu/a1/CANNON
  [VMU] Open file /vmu/a1/CANNON: OK
  [VMU] Loaded settings from CANNON (624 bytes)
  ```
- Expected log on save:
  ```
  [VMU] Open file /vmu/a1/CANNON
  [VMU] Open file /vmu/a1/CANNON: OK
  [VMU] Unlink file /vmu/a1/CANNON
  [VMU] Open file /vmu/a1/CANNON
  [VMU] Open file /vmu/a1/CANNON: OK
  [VMU] Saved settings to CANNON (624 bytes)
  ```
  Save always unlinks + rewrites, so a first-ever save and a resave of an
  existing file look identical in the log — you can't tell "created" from
  "overwritten" from the log alone. To test true first-launch/no-prior-save
  behavior, delete `CANNON` off the physical VMU yourself first (BIOS file
  manager) — there's no local VMU image file in this repo to delete instead;
  hardware test sessions use a physical VMU, not an emulator image.

## Launch (no GDB, self-terminating)

```bash
cd build-dc
kos-tool -t 192.168.0.128 -x cannonball.elf -m cd/
```

Blocks in the foreground until you exit the game or it crashes — that's
normal, just wait. No `-g`, no `sh-elf-gdb`, no `timeout` wrapper needed;
unlike a parking differential build there is no park loop to escape, but
there's also no automatic exit-after-N-frames — you decide when to stop by
choosing **Exit from the in-game menu**, which tears down audio/video
cleanly and returns `Program returned 0` on its own, dropping the console
back to the dc-load prompt. Confirmed hardware-clean: audio/video teardown
logs, then `Program returned 0`, no leftover `kos-tool` process.

If you need to run it in the background while doing other things in the same
shell:

```bash
kos-tool -t 192.168.0.128 -x cannonball.elf -m cd/ > /tmp/run.log 2>&1 &
```

then `tail -f /tmp/run.log` or grep it on demand. Check `ps aux | grep
kos-tool` before assuming it's still running — it exits itself on menu Exit
or crash, same as the foreground case.

### Remote exit (no physical input needed)

There's no host-to-target command channel in this dc-load setup —
`dcload_read()`/`dc/dcload.h` is the dc-load *filesystem* proxy (reads a
file previously opened via `dcload_open()`), not a live stdin/keypress
stream, and a second `kos-tool -r` (reset) connection while a session is
already running fails (`No network loader response` — one connection per
session, same limit GDB hits). So a running session normally needs physical
input to end: the in-game menu's Exit option, or the controller
all-buttons exit chord.

Instead, `main.cpp`'s `tick()` polls for a sentinel file every 30 frames:
`fopen("/pc/exit_now", "rb")`. `kos-tool -m cd/` maps `/pc/` straight to
`build-dc/cd/` on the host — the same passthrough `Config::load()` already
uses for its `/pc/config.xml` fallback — so touching
`build-dc/cd/exit_now` from the host ends a running session on demand:

```bash
touch build-dc/cd/exit_now
# ... game detects it within ~30 frames, calls arch_exit(), kos-tool exits on its own ...
rm -f build-dc/cd/exit_now   # clean up before the next launch
```

Hardware-verified: detected the sentinel, logged
`"cannonball: /pc/exit_now detected, calling arch_exit()"`, went through the
same `"vid_set_mode: 640x480 VGA"` line every other exit path shows (that's
normal `arch_exit()` teardown, not a crash precursor — see below), then
`Program returned 0` and back at the dc-load prompt cleanly. No reboot, no
manual GD-emu menu reload. This is what makes the route-coverage matrix in
`NEXT_TASK.md` (A→A through A→E) actually hands-off: launch at a given
`DREAMCAST_START_LEVEL`, let it collect samples, touch the sentinel, rebuild
for the next route, repeat — no physical chord-pressing between runs.

**Important:** always `rm -f` the sentinel file after use. If it's left
sitting in `build-dc/cd/`, the *next* launch will detect it and exit almost
immediately.

## Two different "state" traces — don't conflate them

The `"cannonball: state %d -> %d frame=%d"` log line (`main.cpp:172`) is
**`cannonball::state`**, the frontend/menu loop enum (`main.hpp`):
`STATE_BOOT=0, STATE_INIT_MENU=1, STATE_MENU=2, STATE_INIT_GAME=3,
STATE_GAME=4, STATE_QUIT=5`. It has nothing to do with `Outrun::game_state`
(`GS_INIT`, `GS_ATTRACT`, `GS_INGAME`, etc. — `engine/outrun.hpp`), which is
the *arcade engine's own* internal state machine and is not logged anywhere
via this trace. It's easy to conflate the two since they share the same log
format and small numeric range — don't. `Outrun::game_state` only starts
advancing once `cannonball::state == STATE_GAME` (`outrun.tick()` is only
called from that case in `main.cpp`'s `tick()`).

There's a **third**, unrelated `Menu::state` enum (`menu.hpp`, private to the
`Menu` class: `STATE_MENU=0, STATE_REDEFINE_KEYS, STATE_REDEFINE_JOY,
STATE_TTRIAL, STATE_DIAGNOSTICS`) — not logged at all, mentioned here only so
a `STATE_MENU` reference in `menu.cpp` isn't mistaken for `cannonball::STATE_MENU`.

## Boot flow: two real gates, and how to skip each

There are two separate, unrelated gates between cold boot and AI-driven
gameplay:

1. **Cannonball's own frontend menu** (`cannonball::STATE_MENU`, `menu.cpp`'s
   `ENTRY_PLAYGAME`/`ENTRY_EXIT` main menu) — a PC-style menu screen that
   waits for a real button press to select "PLAY GAME" before the Outrun
   engine (`outrun.tick()`) ever starts ticking at all. This is a genuine
   manual-input block; a fresh `kos-tool` launch left alone sits here
   forever.
2. **Outrun's internal arcade credit/Start gate** (`Outrun::game_state`,
   `GS_MUSIC`'s `OMusic::check_start()`) — but this one is **not** actually
   a blocker for AI-mode testing: once past gate 1, `Outrun::boot()`
   (`outrun.cpp:105-108`) sets `game_state = GS_INIT` (when
   `layout_debug=0`), which falls straight through to `GS_ATTRACT` and loops
   AI-driven attract-mode gameplay (`GS_ATTRACT` -> `GS_BEST1`/`GS_LOGO` ->
   repeat) **forever with zero input**, because it never gets a credit. Do
   **not** patch `check_freeplay_start()`/`check_start()` to force credits —
   that was tried and it backfires: forcing credits makes the engine jump
   straight from attract mode into real, human-controlled `GS_INGAME`
   almost immediately (confirmed on hardware — "autostarted the game, but
   not in AI driving mode, I was playing"), which is the opposite of what a
   hands-off AI benchmark needs. Only gate 1 needs bypassing.

### `DREAMCAST_AUTOSTART` (cmake option, default OFF)

Bypasses gate 1 only. Implementation: `Menu::tick_ui()` (`menu.cpp`), under
`#ifdef DREAMCAST_AUTOSTART`, calls `start_game(Outrun::MODE_ORIGINAL)` —
the exact same call `ENTRY_PLAYGAME`'s selection makes — after 2 real menu
frames have ticked (`frame == 2`), instead of waiting for a button.

**Do not** call `start_game()`/`autostart()` synchronously in the same
frame as `menu->init()` (i.e. inline in `main.cpp`'s `STATE_INIT_MENU`
case) — that was the first attempt and it **hung the console**, requiring a
physical power cycle. Deferring by a couple of real menu ticks first (the
current implementation) has run clean on hardware repeatedly with no hang.

```bash
cmake -DDREAMCAST_AUTOSTART=ON .
make
```

Verified boot sequence with this on:
```
cannonball: state -1 -> 1 frame=1   <- STATE_BOOT -> STATE_INIT_MENU
cannonball: state 1 -> 2 frame=2    <- STATE_INIT_MENU -> STATE_MENU
cannonball: state 2 -> 3 frame=4    <- STATE_MENU -> STATE_INIT_GAME (autostart fired)
cannonball: state 3 -> 4 frame=5    <- STATE_INIT_GAME -> STATE_GAME
```
Reaches `STATE_GAME` in ~5 frames with zero input, then Outrun's own
`GS_ATTRACT` loop takes over and drives indefinitely on its own.

**Known issue:** running autostart long enough has occasionally hit a crash
(`Program returned 1`) tied to exiting the session, not random mid-play
instability. Two observed signatures so far:
- Empty stack trace, right after a mid-run `"INFO: Double Buffer video
  enabled" / "vid_set_mode: 640x480 VGA"` pair. **Note this line by itself
  is not the problem** — it also appears on the clean sentinel-file
  `arch_exit()` path documented above, which returns to dc-load fine. So
  this signature is something going wrong *around* a video-mode-reset that
  normally succeeds, not the reset itself.
- A real stack trace: `*** ASSERTION FAILURE *** Assertion
  "sm->initialized == 1" failed at sem.c:56 in sem_wait_timed`, inside
  `pvr_txr_load_dma` -> `DC_PVR_UpdateTexture` -> `SDL_UpdateTexture` ->
  `Render::finalize_frame()` (`rendersurface.cpp:380`) -> `arch: aborting
  the system`. A PVR texture-DMA completion semaphore gets waited on after
  it's no longer initialized.

**Impact varies** — sometimes `kos-tool` gets control back on its own
(console fine, no hang), but at least once this required a full physical
reboot and manually reloading dc-load from the GD-emu menu, which is a real
cost for unattended testing. The `/pc/exit_now` -> `arch_exit()` path above
has tested clean on hardware (no crash, no reboot), so prefer that over the
controller exit chord or the in-game menu's Exit option for now when doing
unattended runs, until this is root-caused. Not yet understood: whether the
crash is specific to one exit path (chord vs. menu Exit vs. something else)
or an intermittent race that any exit path can occasionally hit.

### `DREAMCAST_START_LEVEL` (cmake option, default `0` = stage 1)

Starts attract-mode gameplay at a specific stage instead of always stage 1,
for A/B-testing a specific track section without waiting through earlier
stages. Implementation: `OInitEngine::init(int8_t level)`
(`oinitengine.cpp:51-58`) already takes the raw internal stage-lookup byte
directly and sets `oroad.stage_lookup_off = level` — Time Trial mode
(`ttrial.cpp`'s `STAGE_LOOKUP[]`) already used this for human stage
selection; all three of `Outrun`'s `oinitengine.init(...)` call sites now
pass `DREAMCAST_START_LEVEL` instead of a hardcoded `0` for non-TTrial modes.
Falls back to `0` automatically on non-Dreamcast builds (`outrun.cpp`'s
`#ifndef DREAMCAST_START_LEVEL` guard) since it's a shared source file.

Valid values (OutRun's branching 15-stage tree, from `ttrial.cpp`'s
`STAGE_LOOKUP[]`):
```
0x00                          = stage 1
0x09, 0x08                    = stage 2 (two branches)
0x12, 0x11, 0x10               = stage 3
0x1B, 0x1A, 0x19, 0x18         = stage 4
0x24, 0x23, 0x22, 0x21, 0x20   = stage 5
```

```bash
cmake -DDREAMCAST_START_LEVEL=0x12 .
make
```

Hardware-verified: boots straight into stage 3's opening/scenery (visibly
different background at the stage-start screen vs. the default coastal
stage 1 opening), same `STATE_GAME` timing as the default-level boot.

## Perf log field reference

All emitted via `dbglog`/`DC_*_TRACE` under `__DREAMCAST__`, one line per
`*_PERF_INTERVAL_MS` window, values averaged per-frame over that window.

### `spriteperf` (`src/main/hwvideo/hwsprites.cpp`)
```
cannonball: spriteperf fps=<n> sprites=<n> shadow=<n> fullclip=<n> rows=<n> rows_1x=<n>
```
- `sprites` — avg active sprites drawn/frame
- `shadow` — avg sprites using the shadow-blend path
- `fullclip` — avg sprites hitting the full-width no-clip fast path
- `rows` — avg total scanline rows drawn across all sprites
- `rows_1x` — avg rows drawn via the unscaled (1:1 zoom) fast path

### `spritezoom` (only under `DREAMCAST_SPRITE_ZOOM_PROFILE`)
Top-12 hzoom buckets by row count per window, plus a running `cumulative`
total since boot — which horizontal zoom factors (i.e. sprite distances)
dominate raster cost. `hzoom=0x200` (1:1, no zoom) is typically rank #1 by a
wide margin. (A separate baseline effort has referred to this report as
`hzoomprofile`, from before it was implemented/renamed — the log line this
build actually emits is `spritezoom`; go by the current source, not that
older name, if the two ever disagree.)

**Route-coverage methodology for building a sprite-zoom LUT** (surfaced from
a separate baseline-checkpoint effort against this same repo, not something
this session validated — see the toolchain caveat below before trusting its
specific numbers):

- 30 seconds is only the reporting interval, not the total profiling
  duration — the histogram resets after each report, so a *complete* test
  needs to cover the whole game: attract/start through all 5 routes and
  their endings, not just one 30s window.
- OutRun's branching course has 5 route choices (see `DREAMCAST_START_LEVEL`
  above for the internal stage bytes) — for full LUT coverage, record a
  complete run for every route (A→A, A→B, A→C, A→D, A→E), including attract
  mode, crashes, traffic-heavy sections, forks, and endings. Save every
  `spritezoom` report from each run rather than eyeballing a couple of
  samples.
- Use identical graphics settings across every run (resolution, widescreen,
  scale, hires) — `hzoom` bucket distribution shifts with render config, so
  mixed settings between runs contaminate the histogram.
- Ideally repeat on real hardware after emulator testing, per the
  hardware-beats-emulator rule elsewhere in this doc.
- If a lookup table is going to be built from this data, the profiler needs
  a **cumulative** histogram retained across the whole session (in addition
  to the existing per-30s snapshot), printed on request or at the
  ending/game-over screen — otherwise the per-window snapshots have to be
  manually combined by hand to get whole-game frequency data. `hwsprites.cpp`
  already tracks a running `cumulative` total per hzoom bucket internally
  (see the `cumulative-rank` lines in the existing log format) — check
  whether that's sufficient before adding a second tracking mechanism.
- **Toolchain caveat**: the baseline this methodology came from was pinned
  to `sh-elf-g++ 15.2.0` and a specific `dreamcastSDL2` fork commit. This
  session's toolchain is `sh-elf-g++ 17.0.0` (see the `libGL`/`libSDL2` LTO
  mismatch section implicitly covered by the Build section above) — don't
  assume that baseline's historical FPS/row-count numbers (~19-20 FPS,
  ~3,400 sprite rows/frame in heavy sections) still hold without
  re-measuring on the current toolchain and current working tree.

### `videoperf` (`src/main/video.cpp`)
```
cannonball: videoperf fps=<n> total=<n> tile_update=<n> road_bg=<n> tile_bg=<n> tile_fg=<n> road_fg=<n> sprites=<n> text=<n> black=<n>
```
Unlike `spriteperf`'s `sprites` (a count), **every field here is milliseconds
spent per frame in that render stage**. `road_fg` and `sprites` (the ms
stage, not the sprite count) are typically the two dominant costs; `road_bg`
is flat/cheap; `tile_*`/`text`/`black` are usually ~0ms.

### `drawperf` / `renderperf`
```
cannonball: drawperf fps=<n> draw=<n>
cannonball: renderperf fps=<n> update=<n>
```
Coarser per-frame timing around the draw/present and update calls
surrounding the `videoperf` breakdown — useful as a sanity check that
`videoperf`'s stage sum roughly matches overall frame cost.

### `perf` (top-level, `main.cpp`-ish loop)
```
cannonball: perf fps=<n> avg_ms total=<n> tick=<n> prep=<n> render=<n> audio=<n> state=<n> target_fps=<n>
```
Whole-loop breakdown; `state` is the current `game_state` enum value (see
state numbering above) — useful to confirm which phase (attract vs. in-game)
a given perf sample came from.

## Pre-flight checklist

- [ ] `ping -c 2 -W 2 192.168.0.128` responds
- [ ] `ps aux | grep -i "kos-tool\|sh-elf-gdb"` is empty
- [ ] Know which `config.xml` this launch path actually reads (CDI boot vs.
      `kos-tool` dev session — see table above) before editing settings
- [ ] `layout_debug=0` if you want attract-mode AI benchmarking; `=1` only
      if you deliberately want to skip straight to manual/LayOut-debug play
- [ ] If testing fresh-VMU behavior specifically: `CANNON` deleted from the
      physical VMU first (BIOS file manager) — no local VMU image substitutes
      for this on hardware runs
- [ ] After testing: choose Exit from the in-game menu for a clean shutdown;
      confirm `Program returned 0` and no leftover `kos-tool` process before
      launching the next run
