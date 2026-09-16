# Route Matrix Results

## Stage-1 baseline / A→A capture

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
