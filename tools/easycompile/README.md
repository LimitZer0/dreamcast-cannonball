# EasyCompile

A web page that makes a bootable CannonBall (OutRun) disc image for the
Dreamcast from the player's own ROMs. No compiler, KallistiOS or command
line: open `easycompile.html`, drop in the OutRun ROM zip, optionally pick
up to four songs, press **Make disc** and download `cannonball.cdi` (for
Flycast, a GDEMU or similar, or a CD-R).

Everything runs in the browser; nothing is uploaded. The page can be opened
straight from disk or put on any website.

## What it does

- **ROMs:** reads zips (or loose files) and matches every file by CRC-32
  against the revision B set in `src/main/roms.cpp`, so file names don't
  matter. It lists anything missing. The Japanese tracks and the fixed
  sound ROM are added when present.
- **Music (optional):** decodes the songs with the browser (MP3, OGG, WAV,
  FLAC, M4A), resamples to 44.1 kHz and converts them to Yamaha ADPCM WAV,
  the same bytes as `ffmpeg -c:a adpcm_yamaha`. They go in `music/1.wav` to
  `music/4.wav`; players choose SETTINGS > SOUND > MUSIC SOURCE in the game.
- **Disc:** builds the ISO 9660 image at LBA 11702 with `IP.BIN` in front and
  wraps it as an audio/data CDI identical to what `cdi4dc` writes.

- **Recorded sound:** records the original music (with loop points) and the
  FM effects from the player's ROMs, like `build-dc/make_cdi.sh` with
  `RENDER_TOOL`: the same recording program (`wasm/cbrender.js`, built by
  `wasm/build_wasm.sh` with Emscripten) runs in a Web Worker. Takes about
  10-20 seconds. The recordings match the native tool's (same loop points;
  the recorder isn't bit-for-bit repeatable between runs even natively).

## Files

| File | What it is |
|---|---|
| `easycompile.html` | The built page (includes the finished game, no ROMs); `make_tool.py` also copies it to the top of the repository |
| `easycompile.src.html` | The page's source |
| `easycompile_core.js` | ROM matching, ADPCM encoder, ISO and CDI writers (also loads in Node for tests) |
| `wasm/` | The recording tool for the browser: `build_wasm.sh`, `entry.c`, `SDL_config.h`, built `cbrender.js` |
| `cdi_template.gz` | The CDI's fixed parts (audio track, gaps, header) from `cdi4dc` |
| `make_tool.py` | Builds `easycompile.html` |

## Updating the game on the page

After changing the game, rebuild the page with the new binary:

    sh-elf-objcopy -R .stack -O binary cannonball.elf prog.bin
    $KOS_BASE/utils/scramble/scramble prog.bin 1ST_READ.BIN
    tools/easycompile/make_tool.py 1ST_READ.BIN <IP.BIN> [version]

`config.xml` and `res/` come from `build-dc/cd/`.

If the sound code changes, rebuild the recorder first with
`tools/easycompile/wasm/build_wasm.sh`.
