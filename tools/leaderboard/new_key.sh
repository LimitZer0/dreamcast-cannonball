#!/bin/bash
# Makes a random leaderboard key: put it in outrun_config.php (LB_KEY_HEX) on
# the leaderboard site, and give players a leaderboard pass made with it
# (tools/qrleaderboard/qrleaderboard.html).
set -euo pipefail
head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n'; echo
