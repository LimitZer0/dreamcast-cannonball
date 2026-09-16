# Route Matrix Results

## Stage-1 baseline / A→A capture

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

The successful run output was captured by the interactive `kos-tool` session,
but was not redirected to a durable raw-log file. The file
`/tmp/cannonball-route-A-A.log` contains only the failed preliminary launch
(`kos-tool: command not found`) and is not the successful run log.

For future cells, launch with persistent logging:

```bash
source /opt/toolchains/dc/kos/environ.sh
kos-tool -t 192.168.0.128 -x cannonball.elf -m cd/ \
  2>&1 | tee /tmp/cannonball-route-<cell>.log
```

After the run, copy the log to a named results location if it should survive
temporary-file cleanup.
