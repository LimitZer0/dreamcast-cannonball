#!/bin/bash
# Builds the QRLeaderboard setup disc template (setup_template.cdi) with an
# empty PASS.BIN slot, which qrleaderboard.html fills in.
# Needs the KallistiOS environment, genisoimage/mkisofs and cdi4dc.
set -euo pipefail
cd "$(dirname "$0")"
: "${KOS_BASE:?source KallistiOS environ.sh first}"
make ${SETUP_CFLAGS:+KOS_CFLAGS+="$SETUP_CFLAGS"} >/dev/null
MKISOFS=$(command -v mkisofs || command -v genisoimage)
CDI4DC=$(command -v cdi4dc || echo /opt/toolchains/dc/bin/cdi4dc)
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
mkdir "$WORK/cd"
sh-elf-objcopy -R .stack -O binary setup.elf "$WORK/prog.bin"
"$KOS_BASE/utils/scramble/scramble" "$WORK/prog.bin" "$WORK/cd/1ST_READ.BIN"
"$KOS_BASE/utils/makeip/makeip" -f -g "QRLEADERBOARD" -c "QRLEADERBOARD" "$WORK/IP.BIN" >/dev/null
# 512-byte slot: "QRLB", version 0 (empty), then a marker the tool looks for
python3 - "$WORK/cd/PASS.BIN" <<'PY'
import sys
slot = b'QRLB\x00\x00QRLEADERBOARD-PASS-SLOT'
open(sys.argv[1], 'wb').write(slot + bytes(512 - len(slot)))
PY
"$MKISOFS" -quiet -C 0,11702 -V QRLEADERBOARD -G "$WORK/IP.BIN" -r -J -l -o "$WORK/disc.iso" "$WORK/cd"
"$CDI4DC" "$WORK/disc.iso" "${1:-setup_template.cdi}" >/dev/null
echo "Wrote ${1:-setup_template.cdi}"
