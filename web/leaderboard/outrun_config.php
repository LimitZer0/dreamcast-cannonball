<?php
// CannonBall (OutRun) Dreamcast leaderboard: settings.
// Keep this file private (it holds the database password and the game key).
// It can live outside the web root: then change the require line at the top
// of outrun_leaderboard.php to point to it.

// MySQL / MariaDB connection
define('LB_DB_HOST', 'localhost');
define('LB_DB_NAME', 'outrun_leaderboard');
define('LB_DB_USER', 'outrun');
define('LB_DB_PASS', 'change-me');

// Key for this site: players' leaderboard passes (made with
// tools/qrleaderboard/qrleaderboard.html) must use the same one.
// tools/leaderboard/new_key.sh makes a random key.
define('LB_KEY_HEX', '0000000000000000000000000000000000000000000000000000000000000000');   // replace: not set up

// The leaderboard's full address for players' passes (qrleaderboard.html).
// Leave blank to use outrun_leaderboard.php in this folder at the address
// the page was opened from.
define('LB_PUBLIC_URL', '');

// Show the real database error on the page (turn this off once it works)
define('LB_DEBUG', false);

// Page title
define('LB_TITLE', 'OutRun Dreamcast Leaderboard');

// How many rows a leaderboard shows
define('LB_ROWS', 100);
