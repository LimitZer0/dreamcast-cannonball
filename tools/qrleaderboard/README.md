# QRLeaderboard

A small, game-neutral way for Dreamcast homebrew to submit scores online:
the game shows a QR code, the player scans it with a phone, and a web page
stores the scores. Which leaderboard (address and key) is decided by a
**pass** on the player's memory card, not by the game disc, so one disc works
with any leaderboard and with none. Without a pass, games simply don't offer
score submission.

CannonBall for Dreamcast (OutRun) is the first game to support it.

## The pass

A memory card file named `QRLEADERBRD` (2 blocks, shown as "QRLeaderboard"
in the memory card menu). Its data:

| Bytes | Content |
|---|---|
| 4 | `QRLB` |
| 1 | version, 1 |
| 1 | address length, 1–250 |
| n | address (ASCII, `http://` or `https://`) |
| 32 | key |
| 4 | CRC-32 (IEEE, little-endian) of everything before it |

The file has a normal VMU header (description "QRLeaderboard", app ID
`QRLEADERBOARD`) and icon before the data; games search the file for `QRLB`.
Games only read the pass, never write it.

## Making a pass: `qrleaderboard.html`

The page goes on the leaderboard's website, in the same folder as
`qrleaderboard_config.php` (from `web/leaderboard`). That file gives the page
the leaderboard's address and key from the site's settings, so players never
type or see them. (The key is still inside every pass, since the game needs
it: out of sight, not secret.) Opened from disk, or on a site without the
config file, the page says so and makes nothing.

Players open the page, optionally drop in a memory card image, press
**Make the pass**, and download what they need. Everything is made in the
browser; nothing is uploaded.

- `QRLeaderboard_setup.cdi`: a setup disc for a real Dreamcast (GDEMU or
  burned CD). Boot it, choose a memory card with up/down, press A. The pass
  is scrambled on the disc (`PASS.BIN`: "QRLX", nonce, pass XOR keystream)
  and the disc shows only the address, never the key.
- `QRLeaderboard_vmu.bin`: a whole 128 KB memory card image with the pass,
  for Flycast (as `vmu_save_A1.bin`) or SD-card memory cards (VM2, VMU Pro).
  Drop an existing image on the page first to add the pass to it and keep
  its saves.
- `QRLEADER.VMS` + `QRLEADER.VMI`, or `QRLEADERBRD.DCI`: the pass as a single
  save file for memory card managers.

Other leaderboards: `qrleaderboard_config.php` only has to answer with
`{"url": "<leaderboard address>", "key": "<64 hex digits>"}` (the name is
`CONFIG_URL` near the end of `qrleaderboard.src.html`).

## Rebuilding the tool

`qrleaderboard.html` embeds the setup disc and a blank memory card image. To
rebuild it after changing `qrleaderboard.src.html` or the setup disc
(KallistiOS environment needed for the disc):

    setup_disc/build.sh /tmp/setup_template.cdi
    ./make_tool.py /tmp/setup_template.cdi blank_vmu.bin

`setup_disc/make_icon.py` redraws the pass icon (`setup_disc/qr_icon.h`).
