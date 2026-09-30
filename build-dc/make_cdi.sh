#!/bin/bash
# Build a bootable Dreamcast CDI from a CannonBall ELF.
#
#   ./make_cdi.sh <cannonball.elf> <disc-folder> <output.cdi> [title]
#
# <disc-folder> becomes the root of the disc (/cd/): it must contain
# config.xml, res/ and roms/ (your OutRun revision B ROM files).
#
# Optional custom music: put audio files (mp3/ogg/flac/wav/m4a) in
# <disc-folder>/music/ named 1-4 or by title:
#   1 = MAGICAL SOUND SHOWER, 2 = PASSING BREEZE, 3 = SPLASH WAVE,
#   4 = LAST WAVE (high score screen)
# e.g. "1.mp3" or "Passing Breeze.mp3". They are converted to 44.1kHz
# stereo Yamaha ADPCM (music/1.wav ... 4.wav), which the Dreamcast's sound
# chip decodes itself. Needs ffmpeg. Choose SETTINGS > SOUND > MUSIC SOURCE.
#
# Original music and FM sound effects (required): set RENDER_TOOL to a host build of
# CannonBall made with -DRENDER_AUDIO (see tools/render_audio.sh). It plays
# the game's own sound code from the ROMs in <disc-folder>/roms/ and records
# the four tunes (with exact loop points) and the FM chip's six effects, so
# the Dreamcast can stream them instead of emulating the FM chip.
#
# Online leaderboard: nothing to set here. The site address and key come
# from a leaderboard pass on the player's memory card (tools/qrleaderboard).
#
# Needs: KallistiOS environment (environ.sh), genisoimage/mkisofs, cdi4dc.
set -euo pipefail

ELF=${1:?elf}
DISC=${2:?disc folder}
OUT=${3:?output.cdi}
TITLE=${4:-CANNONBALL}

: "${KOS_BASE:?source KallistiOS environ.sh first}"
MKISOFS=$(command -v mkisofs || command -v genisoimage)
CDI4DC=$(command -v cdi4dc || echo /opt/toolchains/dc/bin/cdi4dc)

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

sh-elf-objcopy -R .stack -O binary "$ELF" "$WORK/prog.bin"
"$KOS_BASE/utils/scramble/scramble" "$WORK/prog.bin" "$WORK/1ST_READ.BIN"
"$KOS_BASE/utils/makeip/makeip" -f -g "$TITLE" -c "CANNONBALL" "$WORK/IP.BIN" > /dev/null

mkdir -p "$WORK/cd"
cp "$WORK/1ST_READ.BIN" "$WORK/cd/"
cp -r "$DISC"/. "$WORK/cd/"

# Convert custom music, if any
if [ -d "$WORK/cd/music" ]; then
    shopt -s nullglob nocaseglob
    titles=("MAGICAL SOUND SHOWER" "PASSING BREEZE" "SPLASH WAVE" "LAST WAVE")
    mkdir -p "$WORK/music_out"
    for n in 1 2 3 4; do
        src=""
        for f in "$WORK/cd/music/$n".* "$WORK/cd/music/${titles[$((n-1))]}".*; do src="$f"; break; done
        [ -z "$src" ] && continue
        echo "Music track $n: $(basename "$src")"
        ffmpeg -loglevel error -y -i "$src" -vn -ac 2 -ar 44100 -c:a adpcm_yamaha "$WORK/music_out/$n.wav"
    done
    shopt -u nullglob nocaseglob
    [ -d "$WORK/cd/music/orig" ] && mv "$WORK/cd/music/orig" "$WORK/music_out/orig"
    rm -rf "$WORK/cd/music"
    mv "$WORK/music_out" "$WORK/cd/music"
fi

# Record the original music and FM effects from the ROMs (required: the
# Dreamcast plays these instead of emulating the sound chip)
: "${RENDER_TOOL:?set RENDER_TOOL to the recording tool (see tools/render_audio.sh)}"
if true; then
    [ -x "$RENDER_TOOL" ] || { echo "RENDER_TOOL $RENDER_TOOL not found"; exit 1; }
    R="$WORK/render"
    mkdir -p "$R" "$WORK/cd/music/orig" "$WORK/cd/sfx"
    sed -e "s#/cd/roms/#$WORK/cd/roms/#g" -e "s#/cd/res/#$WORK/cd/res/#g" "$WORK/cd/config.xml" > "$R/config.xml"
    FIX=$(sed -n 's#.*<fix_samples>\([0-9]\)</fix_samples>.*#\1#p' "$WORK/cd/config.xml" | head -1)
    render() { # mode cmd out
        ( cd "$R" && CB_RENDER=$1 CB_CMD=$2 CB_OUT=$3 CB_FIX=${FIX:-1} SDL_AUDIODRIVER=dummy \
          "$RENDER_TOOL" -cfgfile "$R/config.xml" 2>/dev/null | grep -a "cmd 0x" )
    }
    n=1
    for cmd in 0x85 0x81 0x82 0xA5; do
        render music $cmd "$R/$n.wav"
        # 32 kHz stereo Yamaha ADPCM: one byte per sample frame, so the loop
        # start in frames is also its byte offset
        ffmpeg -loglevel error -y -i "$R/$n.wav" -c:a adpcm_yamaha "$WORK/cd/music/orig/$n.wav"
        [ -f "$R/$n.wav.loop" ] && cp "$R/$n.wav.loop" "$WORK/cd/music/orig/$n.wav.loop"
        n=$((n + 1))
    done
    for cmd in 84 86 94 95 99 9B; do
        render sfx 0x$cmd "$WORK/cd/sfx/$cmd.wav"
    done
    rm -rf "$R"
fi

# Optional: keep the finished disc folder and IP.BIN (for building the CDI
# with other tools, e.g. after editing config.xml)
if [ -n "${KEEP_DISC:-}" ]; then
    mkdir -p "$KEEP_DISC"
    rm -rf "$KEEP_DISC/disc"
    cp -r "$WORK/cd" "$KEEP_DISC/disc"
    cp "$WORK/IP.BIN" "$KEEP_DISC/IP.BIN"
fi

"$MKISOFS" -quiet -C 0,11702 -V "$TITLE" -G "$WORK/IP.BIN" -r -J -l -o "$WORK/disc.iso" "$WORK/cd"
"$CDI4DC" "$WORK/disc.iso" "$OUT" > /dev/null
echo "Wrote $OUT"
