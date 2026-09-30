#!/bin/bash
# Build the host tool that records the original music and FM effects from
# the ROMs (used by build-dc/make_cdi.sh when RENDER_TOOL is set).
#
#   tools/render_audio.sh <build-dir>
#   RENDER_TOOL=<build-dir>/cannonball build-dc/make_cdi.sh game.elf disc out.cdi
#
# Needs a Linux host with cmake, SDL2 and Boost headers.
set -e
B=${1:?build dir}
SRC=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$B" && cd "$B"
cmake -DTARGET=linux.cmake -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS=-O2 -DRENDER_AUDIO" "$SRC/cmake" > /dev/null
make -j"$(nproc)" | tail -1
echo "Render tool: $B/cannonball"
