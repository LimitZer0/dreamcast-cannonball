# Route Matrix Results

## Stage-1 baseline / A→A capture

## Stage-4 candidate (`DREAMCAST_START_LEVEL=0x1A`)

- Durable raw log: `logs/route-matrix/stage-0x1A-native320.log`
- Native path and gameplay boot verified: `vid_set_mode: 320x240 VGA`,
  `state 3 -> 4 frame=5`
- First profiling window: `spriteperf fps=26 sprites=46 shadow=41 fullclip=46 rows=1393 rows_1x=54`; cumulative rows `1113433`
- Result: hardware data-address read error; `Program returned 1`
- Address resolution: `genwait_wait` during `SDL_Delay` in `main_loop`
  (`src/main/main.cpp:341`), not a sprite-rasterizer address
- Matrix status: batch stopped before `0x19`; console recovery is required
  before the next upload.

## Stage-5 candidate (`DREAMCAST_START_LEVEL=0x23`)

- Durable raw log: `logs/route-matrix/stage-0x23-native320.log`
- Native path and gameplay boot verified: `vid_set_mode: 320x240 VGA`,
  `state 3 -> 4 frame=5`
- Crash occurred before the first `spriteperf` report
- Result: hardware data-address read error in the audio thread at
  `DREAMCASTAUD_WaitDevice` / `SDL_RunAudio`; `Program returned 1`
- Matrix status: batch stopped before `0x22`; console recovery is required
  before the next upload.

## Stage-5 final candidates (`0x22`, `0x21`, `0x20`)

All three completed cleanly in native 320x240 mode with two profiling windows:

| Start byte | Raw log | Interval 1 / cumulative rows | Interval 2 / cumulative rows | Exit |
|---|---|---|---|---|
| `0x22` | `logs/route-matrix/stage-0x22-native320.log` | 25 FPS, 66 sprites, 792 rows / 596208 | 24 FPS, 47 sprites, 743 rows / 1152658 | `Program returned 0` |
| `0x21` | `logs/route-matrix/stage-0x21-native320.log` | 24 FPS, 47 sprites, 731 rows / 542544 | 29 FPS, 41 sprites, 571 rows / 1046956 | `Program returned 0` |
| `0x20` | `logs/route-matrix/stage-0x20-native320.log` | 27 FPS, 54 sprites, 913 rows / 747609 | 27 FPS, 56 sprites, 799 rows / 1414958 | `Program returned 0` |

## Additional clean captures

These clean raw logs were previously present on disk but omitted from the
summary table:

| Start byte | Raw log | Interval 1 / cumulative rows | Interval 2 / cumulative rows | Exit |
|---|---|---|---|---|
| `0x18` | `logs/route-matrix/stage-0x18-native320.log` | 27 FPS, 59 sprites, 1108 rows / 910961 | 29 FPS, 59 sprites, 628 rows / 1463627 | `Program returned 0` |
| `0x19` | `logs/route-matrix/stage-0x19-native320.log` | 27 FPS, 56 sprites, 912 rows / 747492 | 26 FPS, 57 sprites, 852 rows / 1419570 | `Program returned 0` |
| `0x24` | `logs/route-matrix/stage-0x24-native320.log` | 27 FPS, 55 sprites, 756 rows / 625391 | 29 FPS, 51 sprites, 507 rows / 1071393 | `Program returned 0` |
| `0x10` | `logs/route-matrix/stage-0x10-native320.log` | 23 FPS, 46 sprites, 930 rows / 644618 | 18 FPS, 64 sprites, 1076 rows / 1239722 | `Program returned 0` |
| `0x11` | `logs/route-matrix/stage-0x11-native320.log` | 27 FPS, 54 sprites, 702 rows / 584429 | 29 FPS, 45 sprites, 396 rows / 931140 | `Program returned 0` |
| `0x12` | `logs/route-matrix/stage-0x12-native320.log` | 27 FPS, 58 sprites, 693 rows / 576846 | 29 FPS, 59 sprites, 463 rows / 986653 | `Program returned 0` |
| `0x1B` | `logs/route-matrix/stage-0x1B-native320.log` | 26 FPS, 39 sprites, 545 rows / 429497 | 27 FPS, 32 sprites, 294 rows / 675589 | `Program returned 0` |

## Stage-2 branch candidate (`DREAMCAST_START_LEVEL=0x09`)

- Durable raw log: `logs/route-matrix/stage-2-0x09-native320.log`
- Build: fast sprites, zoom profiler, autostart; start level `0x09`
- Native path verified: `vid_set_mode: 320x240 VGA`; layout `screen=320x240 dst=0,8 320x224`
- Profiling windows captured: 2
- First interval: `spriteperf fps=20 sprites=61 shadow=54 fullclip=61 rows=1269 rows_1x=60`; cumulative rows `769565`
- Second interval: `spriteperf fps=17 sprites=59 shadow=55 fullclip=59 rows=1274 rows_1x=55`; cumulative rows `1455390`
- Exit: `/pc/exit_now` detected; `Program returned 0`; sentinel removed
- Route identity: not assigned; `0x09` identifies the stage-2 start byte, not a confirmed A→B fork.

## Stage-2 branch candidate (`DREAMCAST_START_LEVEL=0x08`)

- Durable raw log: `logs/route-matrix/stage-0x08-native320.log`
- Build: fast sprites, zoom profiler, autostart; start level `0x08`
- Native path and gameplay boot verified: `vid_set_mode: 320x240 VGA`,
  `state 3 -> 4 frame=5`
- Profiling windows captured: 2
- First interval: `spriteperf fps=23 sprites=39 shadow=33 fullclip=39 rows=1018 rows_1x=51`; cumulative rows `722347`
- Second interval: `spriteperf fps=24 sprites=31 shadow=29 fullclip=31 rows=815 rows_1x=45`; cumulative rows `1316923`
- Exit: `/pc/exit_now` detected; `Program returned 0`; sentinel removed
- Route identity: not assigned; `0x08` identifies the stage-2 start byte, not a confirmed A→B fork.

With `0x08` and `0x09` both captured, the stage-2 branch pair is complete —
15 of 15 `STAGE_LOOKUP` bytes now attempted (13 clean, 2 hardware crashes:
`0x1A`, `0x23`).

### Native 320x240 rerun — 2026-09-15

- Durable raw log: `logs/route-matrix/route-A-A-native320-rerun.log`
- Build: `DREAMCAST_FAST_SPRITES=ON`, `DREAMCAST_SPRITE_ZOOM_PROFILE=ON`,
  `DREAMCAST_AUTOSTART=ON`, `DREAMCAST_START_LEVEL=0`
- Default graphics: widescreen `0`, hires `0`, scale `2`, FPS mode `2`,
  vsync `1`
- Native path verified: `screen=320x240 dst=0,8 320x224`; PVR mode
  `320x240 VGA`; texture `320x240`
- Boot/gameplay verified: `state 3 -> 4 frame=5`
- Profiling windows captured: 2
- First interval: `spriteperf fps=28 sprites=40 shadow=34 fullclip=40 rows=816 rows_1x=53`; cumulative rows `694466`
- Second interval: `spriteperf fps=29 sprites=34 shadow=26 fullclip=34 rows=499 rows_1x=49`; cumulative rows `1132116`
- Exit: `/pc/exit_now` detected; `Program returned 0`; sentinel removed
- Route identity: still unverified; stage 1 start is not proof of A→A fork choices.
- **Direct comparison vs. the 640x480-stretch rerun below (same deterministic
  AI route, sprite/road row counts within 2 of each other — the cleanest
  apples-to-apples sample this session):** `drawperf`/`renderperf` are
  identical (`draw=6ms`, `update=3ms` in both), and sprite/road CPU cost is
  unchanged (expected — always computed at the internal 320x224 buffer
  regardless of output resolution). **This is a correctness/quality change,
  not a measured performance win** — the 640x480 GPU stretch blit was
  apparently already cheap on this hardware. Don't cite this rerun as a perf
  improvement; the benefit is exact 1:1 pixel mapping (no stretch distortion)
  matching the original hardware's native display characteristics.

### Rerun — 2026-09-15

- Durable raw log: `logs/route-matrix/route-A-A-rerun.log`
- Boot/gameplay verified: `state 3 -> 4 frame=5`
- Renderer: Dreamcast PVR, 640x480
- Graphics settings: widescreen `1`, hires `0`, scale `2`, FPS mode `2`,
  vsync `1`
- Profiling windows captured: 2
- First interval: `spriteperf fps=28 sprites=40 shadow=34 fullclip=40 rows=818 rows_1x=53`; cumulative rows `692040`
- Second interval: `spriteperf fps=29 sprites=34 shadow=26 fullclip=34 rows=499 rows_1x=49`; cumulative rows `1131843`
- Exit: `/pc/exit_now` detected; `Program returned 0`
- Route identity: still unverified; stage 1 start is not proof of A→A fork choices.

- Hardware: Dreamcast at `192.168.0.128` via BBA/dcload
- Build: `DREAMCAST_FAST_SPRITES=ON`
- Profiling: `DREAMCAST_SPRITE_ZOOM_PROFILE=ON`
- Boot: `DREAMCAST_AUTOSTART=ON`
- Start level: `DREAMCAST_START_LEVEL=0`
- Result: clean sentinel exit; `Program returned 0`
- Exit path: `/pc/exit_now` detected, followed by normal video teardown
- Observed FPS: approximately 15–29 FPS, scene-dependent
- Final cumulative sprite rows reported: `2,087,811`
- Final interval: `spriteperf fps=22 sprites=42 shadow=37 fullclip=42 rows=823 rows_1x=139`
- Final cumulative hzoom leaders: `0x1f8` (`284109` rows), `0x200` (`215745`), `0x3f0` (`86999`)

The earlier successful run output was captured by the interactive `kos-tool`
session, but was not redirected to a durable raw-log file. The file
`/tmp/cannonball-route-A-A.log` contains only the failed preliminary launch
(`kos-tool: command not found`) and is not the successful run log. The rerun
above is the first durable capture.

For future cells, launch with persistent logging:

```bash
source /opt/toolchains/dc/kos/environ.sh
kos-tool -t 192.168.0.128 -x cannonball.elf -m cd/ \
  2>&1 | tee /tmp/cannonball-route-<cell>.log
```

After the run, copy the log to a named results location if it should survive
temporary-file cleanup.
