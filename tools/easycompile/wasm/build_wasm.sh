#!/bin/bash
# Builds cbrender.js: the sound recording tool (the host build with
# -DRENDER_AUDIO, as tools/render_audio.sh) compiled to WebAssembly for
# EasyCompile. Needs Emscripten (tested with 3.1.6, Debian/Ubuntu package
# "emscripten"), plus the SDL2 and Boost headers (libsdl2-dev, libboost-dev):
# only their headers are used, the recording path never calls SDL.
#
#   tools/easycompile/wasm/build_wasm.sh      -> tools/easycompile/wasm/cbrender.js
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
S=$(cd "$HERE/../../../src/main" && pwd)
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
mkdir -p "$W/inc/SDL2" "$W/obj"
ln -s /usr/include/boost "$W/inc/boost"
cp /usr/include/SDL2/*.h "$W/inc/SDL2/"
cp "$HERE/SDL_config.h" "$W/inc/SDL2/SDL_config.h"      # generic config (no platform backend)
FLAGS="-O2 -DRENDER_AUDIO -DNDEBUG -DSDL_MAIN_HANDLED -I$S -I$W/inc -I$W/inc/SDL2"

SRCS="main.cpp romloader.cpp trackloader.cpp roms.cpp video.cpp utils.cpp
      frontend/cabdiag.cpp frontend/config.cpp frontend/menu.cpp frontend/ttrial.cpp
      frontend/vmu.cpp frontend/leaderboard.cpp frontend/timeattack.cpp frontend/qrcodegen.c
      hwvideo/hwroad.cpp hwvideo/hwsprites.cpp hwvideo/hwtiles.cpp
      hwaudio/segapcm.cpp hwaudio/soundchip.cpp hwaudio/ym2151.cpp
      sdl2/audio.cpp sdl2/timer.cpp sdl2/input.cpp sdl2/renderbase.cpp sdl2/rendersurface.cpp
      engine/oanimseq.cpp engine/oattractai.cpp engine/obonus.cpp engine/ocrash.cpp
      engine/oferrari.cpp engine/ohiscore.cpp engine/ohud.cpp engine/oinitengine.cpp
      engine/oinputs.cpp engine/olevelobjs.cpp engine/ologo.cpp engine/omap.cpp engine/omusic.cpp
      engine/ooutputs.cpp engine/opalette.cpp engine/oroad.cpp engine/osmoke.cpp engine/osprite.cpp
      engine/osprites.cpp engine/ostats.cpp engine/otiles.cpp engine/otraffic.cpp engine/outils.cpp
      engine/outrun.cpp engine/audio/osound.cpp engine/audio/osoundint.cpp"
for f in $SRCS; do
    src="$S/$f"
    # clang rejects ohud.cpp's multi-character literal (never matches a char
    # either way); compile a copy with its value spelled out
    if [ "$f" = engine/ohud.cpp ]; then
        sed "s/'\xef\xbf\xbd'/(int)0xEFBFBD/" "$src" > "$W/ohud.cpp"
        src="$W/ohud.cpp"
    fi
    case "$f" in *.c) C=emcc ;; *) C=em++ ;; esac
    $C $FLAGS -I"$(dirname "$S/$f")" -c "$src" -o "$W/obj/$(echo "$f" | tr / _).o"
done
emcc -O2 -c "$HERE/entry.c" -o "$W/obj/entry.o"
em++ -O2 "$W"/obj/*.o -o "$HERE/cbrender.js" \
    -sMODULARIZE=1 -sEXPORT_NAME=CBRender -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1 \
    -sERROR_ON_UNDEFINED_SYMBOLS=0 -sEXPORTED_FUNCTIONS=_cb_run \
    -sEXPORTED_RUNTIME_METHODS='["FS","ENV"]' -sENVIRONMENT=web,worker,node -sSINGLE_FILE=1 \
    2> >(grep -v "undefined symbol" >&2)
echo "wrote $HERE/cbrender.js"
