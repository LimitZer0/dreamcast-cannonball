# OutRun Dreamcast leaderboard (website)

A small PHP + MySQL site for the scores players send from the game's high
score screen. Players with a QRLeaderboard pass on their memory card (see
below) get SUBMIT SCORES in the main menu, which shows a QR code holding a
table's scores
(only entries the player made, not the arcade's default names). Scanning it
opens this site, which checks the code, lists the scores and asks for a user
name (no password). The leaderboard shows the best scores, highest first, for
each of the game's 8 high score tables, with route and time, plus the 4 Time
Trial tables (fastest first). It can be filtered by goal (A–E) or by one exact
route.

## Files

| File | What it is |
|---|---|
| `outrun_leaderboard.php` | The whole site (leaderboard, and the page the QR code opens) |
| `outrun_config.php` | Database login and the key shared with the game. Keep it private. |
| `qrleaderboard.html` | The pass maker players use: copy it from `tools/qrleaderboard` |
| `qrleaderboard_config.php` | Gives the pass maker this site's address and key from `outrun_config.php` |
| `schema.sql` | Creates the database and its two tables |

## Install

1. **Database.** Create a MySQL/MariaDB database and a user with SELECT and
   INSERT rights on it, then run `schema.sql` (from the command line with
   `mysql -u root -p < schema.sql`, or paste the `CREATE TABLE` statements
   into phpMyAdmin on shared hosting).
2. **Settings.** Edit `outrun_config.php`: database host, name, user and
   password, and `LB_KEY_HEX` (the key, see below: it ships as zeros, which
   the site treats as not set up).
3. **Upload** `outrun_leaderboard.php`, `outrun_config.php`,
   `qrleaderboard.html` and `qrleaderboard_config.php` to the same
   folder on your web host (PHP 7.4 or newer with PDO MySQL; use HTTPS).
   Open `outrun_leaderboard.php` in a browser: you should see an empty
   leaderboard.
4. **Give players a pass.** The game disc holds no address or key: players
   add a QRLeaderboard pass to their memory card. Point them to
   `qrleaderboard.html` on your site; it fills in the address and key itself
   (players don't see or type them). The address it uses is
   `outrun_leaderboard.php` in the same folder; set `LB_PUBLIC_URL` in
   `outrun_config.php` if that comes out wrong (e.g. behind a proxy). See
   `tools/qrleaderboard/README.md`.

Moving the site later means everyone makes a new pass, so a short,
permanent address is best.

## The key

The game encrypts and signs the scores with a 32-byte key; the site only
accepts codes signed with the same key. The key is `LB_KEY_HEX` in
`outrun_config.php`, and players' passes carry it too.
`tools/leaderboard/new_key.sh` makes a random one.

This stops people from typing in made-up scores or editing a code, but it
isn't real anti-cheat: the key is in every player's pass and can be
extracted by anyone determined enough, and the game itself can be modified. The site also
rejects scores the game can't produce (not a multiple of 10, out of order,
impossible times or routes).

## Notes

- **User names** are 3–20 letters, numbers, spaces, `_ . -`, and are claimed
  by whoever uses them first (case doesn't matter). There are no passwords,
  so anyone can submit under any name.
- **Duplicates:** each entry is stored once. Rescanning the same table only
  adds the new entries. An entry belongs to whoever submits it first, so
  someone who scans another player's QR code before they do could claim it.
- **Tables:** World/Japan tracks × Arcade/Continuous × normal/Modified
  (grippy tyres, off-road, bumper, turbo, timer off or the prototype
  stage 1), the same split as
  the game's own high score tables.
- **Time Trial** (the game's full-course mode with no traffic and no
  countdown) has its own tables: World/Japan tracks × normal/Modified (grippy
  tyres, off-road, bumper, turbo or the prototype stage 1). Its QR codes hold every saved time (up to
  30 per code; more make extra pages), and the site keeps each player's
  fastest time, ranked by time. The same database table holds them
  (`score_table` 8–11, score 0), so no database change is needed.
- **Routes** are stored as the game's course map position (0 = stage 1,
  15–30 = the 16 complete routes). The route arrows on the site read like the
  course map: ↑ is the fork towards goal A's side, ↓ towards goal E's.
- **Times** are the total time the game shows for a finished run. A run that
  ran out of time shows the stage it reached and no time.
- The code format is described in the game source,
  `src/main/frontend/leaderboard.hpp`.
