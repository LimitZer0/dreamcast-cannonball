#!/bin/bash
# One command to build CannonBall for the Dreamcast: cannonball.cdi.
#
#   build-dc/build.sh
#
# Before running it, put your OutRun revision B ROMs in build-dc/cd/roms/
# (and any custom songs in build-dc/cd/music/, see docs/BUILDING-DREAMCAST.md).
#
# The first run sets up everything it needs (Ubuntu/Debian or WSL; asks for
# your password to install packages) and takes a while: the Dreamcast
# compiler is built from source. Later runs skip the setup and only build
# the game and the disc (a few minutes).
set -euo pipefail

DC=/opt/toolchains/dc
REPO=$(cd "$(dirname "$0")/.." && pwd)
RENDER=$HOME/.cache/cannonball-render
step() { echo; echo "==> $*"; }

# --- ROMs first, so a missing set fails now and not after an hour ---------
ROMS=$(find "$REPO/build-dc/cd/roms" -maxdepth 1 -type f ! -iname '*.txt' | wc -l)
if [ "$ROMS" -lt 31 ]; then
    echo "Put your OutRun revision B ROM files (unzipped) in build-dc/cd/roms/ first."
    echo "Found $ROMS file(s); the set has 31."
    exit 1
fi

# --- 1. Packages ----------------------------------------------------------
PKGS="build-essential git cmake texinfo libjpeg-dev libpng-dev libelf-dev python3
      wget genisoimage ffmpeg libsdl2-dev libboost-dev"
if ! dpkg -s $PKGS >/dev/null 2>&1; then
    step "Installing packages"
    sudo apt-get update
    sudo apt-get install -y $PKGS
fi

if [ ! -d "$DC" ]; then
    sudo mkdir -p "$DC"
    sudo chown "$USER" "$DC"
fi

# --- 2. KallistiOS and the Dreamcast compiler -----------------------------
[ -d "$DC/kos" ] || git clone https://github.com/KallistiOS/KallistiOS "$DC/kos"
if [ ! -x "$DC/sh-elf/bin/sh-elf-gcc" ]; then
    step "Building the Dreamcast compiler (the long part)"
    cd "$DC/kos/utils/kos-chain"
    [ -f Makefile.cfg ] || cp Makefile.dreamcast.cfg Makefile.cfg
    make
    make distclean              # remove the ~7 GB of build leftovers
fi
[ -f "$DC/kos/environ.sh" ] || cp "$DC/kos/doc/environ.sh.sample" "$DC/kos/environ.sh"
set +u; source "$DC/kos/environ.sh"; set -u
if [ ! -f "$KOS_BASE/lib/dreamcast/libkallisti.a" ]; then
    step "Building KallistiOS"
    make -C "$KOS_BASE"
fi

# --- 3. GLdc and SDL2 ------------------------------------------------------
if [ ! -f "$KOS_BASE/addons/lib/dreamcast/libGL.a" ]; then
    step "Building GLdc"
    [ -d "$DC/kos-ports" ] || git clone https://github.com/KallistiOS/kos-ports "$DC/kos-ports"
    make -C "$DC/kos-ports/libGL" install clean
fi
if [ ! -f "$KOS_BASE/addons/lib/dreamcast/libSDL2.a" ]; then
    step "Building SDL2 for the Dreamcast"
    [ -d "$DC/SDL2-dc" ] || git clone -b dreamcastSDL2 https://github.com/GPF/SDL "$DC/SDL2-dc"
    cd "$DC/SDL2-dc/build-scripts"
    ./dreamcast.sh install
fi

# --- 4. Disc tool ----------------------------------------------------------
if ! command -v cdi4dc >/dev/null && [ ! -x "$DC/bin/cdi4dc" ]; then
    step "Building cdi4dc"
    [ -d "$DC/img4dc" ] || git clone https://github.com/Kazade/img4dc "$DC/img4dc"
    cmake -S "$DC/img4dc" -B "$DC/img4dc/build"
    cmake --build "$DC/img4dc/build"
    mkdir -p "$DC/bin"
    cp "$DC/img4dc/build/cdi4dc/cdi4dc" "$DC/bin/"
fi

# --- Recording tool (records the original music and effects) --------------
if [ ! -x "$RENDER/cannonball" ]; then
    step "Building the sound recording tool"
    "$REPO/tools/render_audio.sh" "$RENDER"
fi

# --- 5. The game -----------------------------------------------------------
step "Building the game"
cd "$REPO/build-dc"
./dreamcast.sh

# --- 6. The disc -----------------------------------------------------------
step "Making the disc"
RENDER_TOOL="$RENDER/cannonball" ./make_cdi.sh cannonball.elf cd cannonball.cdi

echo
echo "Done: $REPO/build-dc/cannonball.cdi"
if grep -qi microsoft /proc/version 2>/dev/null; then
    echo "In Windows: \\\\wsl\$\\${WSL_DISTRO_NAME:-Ubuntu}$(echo "$REPO/build-dc" | tr / '\\')"
fi
