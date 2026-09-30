#!/bin/bash
# Headless score measurement for the difficulty multipliers (host build).
#
# Build (Linux, needs SDL2 + Boost headers):
#   cmake -DTARGET=linux.cmake -DCMAKE_BUILD_TYPE=Release \
#     "-DCMAKE_CXX_FLAGS=-O2 -DSCORE_SIM -DDREAMCAST_FORCE_AI -DDREAMCAST_SKIP_CREDITS \
#      -DDREAMCAST_AUTOSTART -DDREAMCAST_SKIP_HISCORE_ENTRY" ../../cmake && make
# Run from a folder holding roms/, res/ and a config.xml whose paths point there:
#   run.sh JAP TIME TRAFFIC GRIP GAMES SEED   (TRAFFIC -1 = disabled)
# Env: CANNONBALL (binary), SCALE=1 to apply the multipliers, BUMPER=1.
# Prints one line per game; summarise with stats.py / fit.py.
CANNONBALL=${CANNONBALL:-./cannonball}
CB_SCALE=${SCALE:-0} CB_BUMPER=${BUMPER:-0} CB_JAP=$1 CB_TIME=$2 CB_TRAFFIC=$3 CB_GRIP=$4 CB_GAMES=$5 CB_SEED=$6 \
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$CANNONBALL" -cfgfile config.xml 2>/dev/null \
  | grep -a RESULT | sed "s/^/J=$1 T=$2 R=$3 G=$4 /"
