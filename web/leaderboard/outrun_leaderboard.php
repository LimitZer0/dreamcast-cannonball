<?php
/*
 * CannonBall (OutRun) Dreamcast leaderboard.
 *
 *   outrun_leaderboard.php              the leaderboard
 *   outrun_leaderboard.php?d=<code>     opened from the QR code on the game's
 *                                       high score screen: shows the scores
 *                                       and asks for a user name
 *
 * Needs PHP 7.4+ with PDO MySQL. Settings: outrun_config.php. Database:
 * schema.sql. The code format is described in the game source
 * (src/main/frontend/leaderboard.hpp).
 */
declare(strict_types=1);

require __DIR__ . '/outrun_config.php';

const LB_MAX_CODE   = 1200;   // longest code accepted (20 entries is ~390)
const LB_NONCE_LEN  = 8;
const LB_MAC_LEN    = 10;
const LB_ENTRY_LEN  = 11;
const LB_NAME_COOKIE = 'outrun_lb_name';

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function h($s): string
{
    return htmlspecialchars((string)$s, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

function self_url(array $params = []): string
{
    $path = strtok($_SERVER['REQUEST_URI'] ?? '', '?') ?: basename(__FILE__);
    return $params ? $path . '?' . http_build_query($params) : $path;
}

function lb_db(): PDO
{
    static $pdo = null;
    if ($pdo === null) {
        $pdo = new PDO(
            'mysql:host=' . LB_DB_HOST . ';dbname=' . LB_DB_NAME . ';charset=utf8mb4',
            LB_DB_USER,
            LB_DB_PASS,
            [
                PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
                PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
                PDO::ATTR_EMULATE_PREPARES   => false,
            ]
        );
    }
    return $pdo;
}

// Tables 0-7: the game's high score tables. 8-11: time trial (full course,
// no traffic): 8 + Japan (1) + modified (2). Time trial rows have score 0 and
// rank by time.
const LB_TT_BASE = 8;
function is_tt(int $t): bool
{
    return $t >= LB_TT_BASE;
}

function table_name(int $t): string
{
    if (is_tt($t)) {
        return 'Time Trial ' . (($t & 1) ? 'Japan' : 'World') . (($t & 2) ? ' Modified' : '');
    }
    return (($t & 1) ? 'Japan' : 'World') . ' ' .
           (($t & 2) ? 'Continuous' : 'Arcade') .
           (($t & 4) ? ' Modified' : '');
}

function format_score(int $score): string
{
    return number_format($score);
}

// Time as the game shows it: 4'20"13
// When a score was submitted: UTC on the page, shown in the viewer's time
// zone by the script under the table
function submitted_html(int $ts): string
{
    if ($ts <= 0) return '';
    return '<time datetime="' . gmdate('Y-m-d\\TH:i:s\\Z', $ts) . '" data-ts="' . $ts . '" title="' . gmdate('Y-m-d H:i', $ts) . ' UTC">'
         . '<span class="d">' . gmdate('M j, Y', $ts) . '</span><span class="t"> ' . gmdate('H:i', $ts) . ' UTC</span></time>';
}

function format_time(?int $cs): string
{
    if (!$cs) {
        return '—';
    }
    $m = intdiv($cs, 6000);
    $s = intdiv($cs % 6000, 100);
    $c = $cs % 100;
    return sprintf("%d'%02d\"%02d", $m, $s, $c);
}

// The game's initials are its own tile codes: A-Z, '[' is the full stop
function initials_text(string $raw): string
{
    $out = '';
    for ($i = 0; $i < 3; $i++) {
        $c = $raw[$i] ?? ' ';
        if ($c >= 'A' && $c <= 'Z') {
            $out .= $c;
        } elseif ($c === '[' || $c === '.') {
            $out .= '.';
        } elseif ($c === ' ') {
            $out .= ' ';
        } else {
            $out .= '?';
        }
    }
    return rtrim($out) === '' ? '...' : $out;
}

// ---------------------------------------------------------------------------
// Routes
//
// Route numbers follow the in-game course map: 0 = stage 1, then each stage
// doubles (1-2, 3-6, 7-14, 15-30). The bits of (route - first of its stage),
// from the top, are the forks taken: 0 = towards goal A's side of the map,
// 1 = towards goal E's side. The location reached on a stage is the count
// of 1 forks so far, and on stage 5 that's the goal: A + location.
// ---------------------------------------------------------------------------

function route_info(?int $route): ?array
{
    if ($route === null || $route < 0 || $route > 30) {
        return null;
    }
    $stage = 0;
    while ((1 << ($stage + 1)) - 1 <= $route) {
        $stage++;
    }
    $pos = $route - ((1 << $stage) - 1);
    $forks = [];
    $locs = [0];
    for ($i = $stage - 1; $i >= 0; $i--) {
        $bit = ($pos >> $i) & 1;
        $forks[] = $bit;
        $locs[] = end($locs) + $bit;
    }
    return [
        'stage' => $stage + 1,                       // 1-5 reached
        'forks' => $forks,
        'locs'  => $locs,
        'goal'  => $stage === 4 ? chr(65 + $locs[4]) : null,
    ];
}

function route_arrows(array $forks): string
{
    $s = '';
    foreach ($forks as $f) {
        $s .= $f ? '↓' : '↑';
    }
    return $s;
}

function route_label(?int $route): string
{
    $r = route_info($route);
    if (!$r) {
        return '—';
    }
    if ($r['goal']) {
        return 'Goal ' . $r['goal'];
    }
    return 'Stage ' . $r['stage'];
}

// Little course map: 5 stages left to right, goal A's side at the top
function route_svg(?int $route): string
{
    $r = route_info($route);
    if (!$r) {
        return '';
    }
    $dx = 18; $dy = 9; $w = 4 * $dx + 12; $hgt = 4 * $dy + 12;
    $pt = function (int $stage, int $loc) use ($dx, $dy, $hgt): array {
        return [6 + $stage * $dx, $hgt / 2 + ($loc - $stage / 2) * $dy];
    };
    $svg = '<svg class="map" viewBox="0 0 ' . $w . ' ' . $hgt . '" width="' . $w . '" height="' . $hgt . '" aria-hidden="true">';
    for ($s = 0; $s < 5; $s++) {
        for ($l = 0; $l <= $s; $l++) {
            [$x, $y] = $pt($s, $l);
            $svg .= '<circle cx="' . $x . '" cy="' . $y . '" r="2" class="dot"/>';
        }
    }
    $pts = [];
    foreach ($r['locs'] as $s => $l) {
        $pts[] = implode(',', $pt($s, $l));
    }
    $svg .= '<polyline points="' . implode(' ', $pts) . '" class="path"/>';
    [$x, $y] = $pt(count($r['locs']) - 1, end($r['locs']));
    $svg .= '<circle cx="' . $x . '" cy="' . $y . '" r="3.5" class="end"/>';
    return $svg . '</svg>';
}

// Filter choices: all, a goal, or one complete route (15-30)
function route_filters(): array
{
    $f = ['all' => 'All routes'];
    foreach (range('A', 'E') as $g) {
        $f['goal-' . $g] = 'Goal ' . $g . ' (any route)';
    }
    for ($route = 15; $route <= 30; $route++) {
        $r = route_info($route);
        $f['route-' . $route] = 'Goal ' . $r['goal'] . '  ' . route_arrows($r['forks']);
    }
    return $f;
}

// ---------------------------------------------------------------------------
// Code from the game
// ---------------------------------------------------------------------------

function b32_decode(string $s): ?string
{
    static $alpha = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';
    $s = strtoupper(trim($s));
    $out = '';
    $acc = 0;
    $bits = 0;
    $n = strlen($s);
    for ($i = 0; $i < $n; $i++) {
        $v = strpos($alpha, $s[$i]);
        if ($v === false) {
            return null;
        }
        $acc = (($acc << 5) | $v) & 0xFFFF;
        $bits += 5;
        if ($bits >= 8) {
            $bits -= 8;
            $out .= chr(($acc >> $bits) & 0xFF);
        }
    }
    return $out;
}

/**
 * Checks and decrypts a code. Returns the scores, or null with $error set.
 */
function lb_decode(string $code, ?string &$error): ?array
{
    $error = null;
    if ($code === '' || strlen($code) > LB_MAX_CODE) {
        $error = 'This code is empty or too long.';
        return null;
    }
    $pkt = b32_decode($code);
    if ($pkt === null || strlen($pkt) < 1 + LB_NONCE_LEN + 3 + 7 + LB_MAC_LEN) {
        $error = 'This code is damaged. Try scanning it again.';
        return null;
    }
    if (ord($pkt[0]) !== 1) {
        $error = 'This code is from a newer version of the game.';
        return null;
    }

    if (!preg_match('/^[0-9a-f]{64}$/', LB_KEY_HEX) || trim(LB_KEY_HEX, '0') === '') {
        $error = 'The leaderboard key is not set up (LB_KEY_HEX in outrun_config.php).';
        return null;
    }
    $key  = hex2bin(LB_KEY_HEX);
    $kenc = hash_hmac('sha256', 'enc', $key, true);
    $kmac = hash_hmac('sha256', 'mac', $key, true);

    $body = substr($pkt, 0, -LB_MAC_LEN);
    $mac  = substr($pkt, -LB_MAC_LEN);
    if (!hash_equals(substr(hash_hmac('sha256', $body, $kmac, true), 0, LB_MAC_LEN), $mac)) {
        $error = 'This code could not be verified. It may be damaged, or made by a different build of the game.';
        return null;
    }

    $nonce = substr($body, 1, LB_NONCE_LEN);
    $ct    = substr($body, 1 + LB_NONCE_LEN);
    $plain = '';
    for ($off = 0, $block = 0; $off < strlen($ct); $off += 32, $block++) {
        $ks = hash_hmac('sha256', $nonce . pack('V', $block), $kenc, true);
        $plain .= substr($ct, $off, 32) ^ substr($ks, 0, min(32, strlen($ct) - $off));
    }

    $table  = ord($plain[0]);
    $flags  = ord($plain[1]);
    $count  = ord($plain[2]);
    $entries = [];

    if ($table & 0x80) {
        // Time trial: route 0-15 | initials | time
        if (($table & ~0x85) || $count < 1 || $count > 30 || strlen($plain) !== 3 + $count * 7) {
            $error = 'This code has an unexpected layout.';
            return null;
        }
        $site_table = LB_TT_BASE + ($table & 1) + (($table & 4) ? 2 : 0);
        for ($i = 0; $i < $count; $i++) {
            $e = substr($plain, 3 + $i * 7, 7);
            $r  = ord($e[0]);
            $cs = unpack('V', substr($e, 4, 3) . "\0")[1];
            if ($r > 15 || $cs < 60 * 100 || $cs >= 60 * 60 * 100) {
                $error = 'This code contains a time the game could not have made.';
                return null;
            }
            $info = route_info(15 + $r);
            $entries[] = [
                'score'    => 0,
                'raw_init' => substr($e, 1, 3),
                'initials' => initials_text(substr($e, 1, 3)),
                'route'    => 15 + $r,
                'goal'     => $info['goal'],
                'time_cs'  => $cs,
            ];
        }
        $table = $site_table;
        $flags = 1;
    } else {
        if ($table > 7 || $count < 1 || $count > 20 || strlen($plain) !== 3 + $count * LB_ENTRY_LEN) {
            $error = 'This code has an unexpected layout.';
            return null;
        }
        $prev = PHP_INT_MAX;
        for ($i = 0; $i < $count; $i++) {
            $e = substr($plain, 3 + $i * LB_ENTRY_LEN, LB_ENTRY_LEN);
            $score = unpack('V', substr($e, 0, 4))[1];
            $init  = substr($e, 4, 3);
            $route = ord($e[7]);
            $cs    = unpack('V', substr($e, 8, 3) . "\0")[1];
            $route = $route === 255 ? null : $route;

            // The game can't make these: reject the whole code
            $bad = $score <= 0 || $score > 99999990 || $score % 10 !== 0 || $score > $prev
                || ($route !== null && $route > 30)
                || ($cs !== 0 && ($cs < 60 * 100 || $cs >= 60 * 60 * 100));
            if ($bad) {
                $error = 'This code contains a score the game could not have made.';
                return null;
            }
            $prev = $score;
            $info = route_info($route);
            $entries[] = [
                'score'    => $score,
                'raw_init' => $init,
                'initials' => initials_text($init),
                'route'    => $route,
                'goal'     => $info ? $info['goal'] : null,
                'time_cs'  => $cs ?: null,
            ];
        }
    }

    $ts = unpack('V', substr($nonce, 0, 4))[1];
    return [
        'table'        => $table,
        'scaled'       => ($flags & 1) ? 1 : 0,
        // Only keep a believable console clock (after 2020, not in the future)
        'console_time' => ($ts > 1577836800 && $ts < time() + 86400) ? gmdate('Y-m-d H:i:s', $ts) : null,
        'entries'      => $entries,
    ];
}

function fingerprint(int $table, array $e): string
{
    return hash('sha256', implode('|', [
        $table, $e['score'], bin2hex($e['raw_init']), $e['route'] ?? 'x', $e['time_cs'] ?? 0,
    ]), true);
}

function clean_username(string $name): ?string
{
    $name = trim(preg_replace('/\s+/u', ' ', $name));
    if (!preg_match('/^[A-Za-z0-9 _.\-]{3,20}$/', $name)) {
        return null;
    }
    return $name;
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

function page_start(string $title): void
{
    ?><!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title><?= h($title) ?></title>
<style>
:root {
    --bg: #10131f; --panel: #1a1f33; --line: #2c3352; --text: #e8ecf8;
    --muted: #97a0bf; --accent: #ffcf3f; --hot: #ff5c7a; --ok: #53d99a;
    --path: #ffcf3f; --dot: #4a5378;
    color-scheme: dark;     /* always dark, including form controls */
}
* { box-sizing: border-box; }
body {
    margin: 0; background: var(--bg); color: var(--text);
    font: 16px/1.45 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
}
header {
    background: linear-gradient(180deg, #ff8a3d 0%, #ff4f7b 55%, #6d3bd2 100%);
    color: #fff; padding: 22px 16px 18px; text-align: center;
}
header h1 { margin: 0; font-size: clamp(22px, 5vw, 34px); letter-spacing: .04em; text-shadow: 0 2px 0 rgba(0,0,0,.25); }
header p { margin: 4px 0 0; opacity: .9; }
main { max-width: 920px; margin: 0 auto; padding: 16px; }
.panel { background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 16px; margin-bottom: 16px; }
.filters { display: flex; flex-wrap: wrap; gap: 12px; align-items: end; }
.filters label { display: flex; flex-direction: column; font-size: 13px; color: var(--muted); gap: 4px; }
.filters .check { flex-direction: row; align-items: center; gap: 6px; font-size: 15px; color: var(--text); }
select, input[type=text], button {
    font: inherit; color: var(--text); background: var(--bg); border: 1px solid var(--line);
    border-radius: 8px; padding: 8px 10px; min-height: 40px;
}
button { background: var(--accent); color: #1d1300; border: 0; font-weight: 700; cursor: pointer; padding: 8px 16px; }
table { width: 100%; border-collapse: collapse; }
th, td { padding: 8px 6px; border-bottom: 1px solid var(--line); text-align: left; vertical-align: middle; }
th { font-size: 12px; text-transform: uppercase; letter-spacing: .06em; color: var(--muted); font-weight: 600; }
td.num, th.num { text-align: right; font-variant-numeric: tabular-nums; }
td.rank { color: var(--muted); width: 2.5em; }
tr.top1 td.rank { color: var(--accent); font-weight: 700; }
tr.mine td { background: color-mix(in srgb, var(--accent) 12%, transparent); }
.score { font-weight: 700; font-variant-numeric: tabular-nums; }
.initials { font-family: ui-monospace, Menlo, Consolas, monospace; color: var(--muted); }
.route { display: flex; align-items: center; gap: 8px; white-space: nowrap; }
.map .dot { fill: var(--dot); }
.map .path { fill: none; stroke: var(--path); stroke-width: 2; stroke-linejoin: round; }
.map .end { fill: var(--path); }
.note { color: var(--muted); font-size: 14px; }
a { color: var(--accent); }
.scroll { overflow-x: auto; }
.msg { padding: 12px 14px; border-radius: 8px; margin-bottom: 16px; border: 1px solid var(--line); }
.msg.ok { border-color: var(--ok); }
.msg.err { border-color: var(--hot); }
.tag { font-size: 11px; border: 1px solid var(--line); border-radius: 4px; padding: 1px 4px; color: var(--muted); margin-left: 4px; }
.new { color: var(--ok); font-weight: 600; }
.old { color: var(--muted); }
td.when { color: var(--muted); font-size: 14px; white-space: nowrap; font-variant-numeric: tabular-nums; }
.when-sm { display: none; color: var(--muted); font-size: 12px; }
form.submit { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; }
form.submit input[type=text] { flex: 1 1 200px; }
@media (max-width: 560px) {
    th.hide-sm, td.hide-sm { display: none; }
    th, td { padding: 8px 3px; }
    body { font-size: 14px; }
    .tag { display: none; }
    .map { display: none; }
    th.when, td.when { display: none; }       /* phones: the date goes under the name */
    .when-sm { display: block; }
    .when-sm .t { display: none; }
    main { padding: 12px; }
}
</style>
</head>
<body>
<header>
    <h1><?= h(LB_TITLE) ?></h1>
    <p>CannonBall for Dreamcast</p>
</header>
<main>
<?php
}

function page_end(): void
{
    ?>
<p class="note">Scores and time trial times come from the QR codes under SUBMIT SCORES in the game's main menu (shown when a QRLeaderboard pass is on the memory card).
Route arrows follow the in-game course map: ↑ is the fork towards goal A's side, ↓ towards goal E's.</p>
</main>
</body>
</html>
<?php
}

// Database problem. With LB_DEBUG on (outrun_config.php) the page shows the
// actual MySQL/PDO error, which says what's wrong (login, database name,
// missing tables, missing PDO MySQL driver...).
function db_error_page(Throwable $ex): void
{
    $msg = 'The leaderboard database is not available right now.';
    if (defined('LB_DEBUG') && LB_DEBUG) {
        $msg .= ' Error: ' . $ex->getMessage();
    }
    error_log('outrun_leaderboard: ' . $ex->getMessage());
    error_page($msg, 503);
}

function error_page(string $message, int $status = 400): void
{
    http_response_code($status);
    page_start(LB_TITLE);
    echo '<div class="msg err">' . h($message) . '</div>';
    echo '<p><a href="' . h(self_url()) . '">View the leaderboard</a></p>';
    page_end();
}

// Opened from the QR code: show the scores and ask for a name
function show_submit_form(string $code): void
{
    $data = lb_decode($code, $err);
    if (!$data) {
        error_page($err);
        return;
    }

    // Which entries are already on the board
    $known = [];
    try {
        $fps = array_map(fn($e) => fingerprint($data['table'], $e), $data['entries']);
        $in  = implode(',', array_fill(0, count($fps), '?'));
        $st  = lb_db()->prepare("SELECT fingerprint FROM scores WHERE fingerprint IN ($in)");
        $st->execute($fps);
        foreach ($st->fetchAll(PDO::FETCH_COLUMN) as $fp) {
            $known[$fp] = true;
        }
    } catch (PDOException $ex) {
        db_error_page($ex);
        return;
    }
    $new = 0;
    foreach ($data['entries'] as $e) {
        if (empty($known[fingerprint($data['table'], $e)])) {
            $new++;
        }
    }

    page_start('Submit scores – ' . LB_TITLE);
    $name = $_COOKIE[LB_NAME_COOKIE] ?? '';
    ?>
<div class="panel">
    <?php $tt = is_tt($data['table']); ?>
    <h2 style="margin-top:0">Your <?= h(table_name($data['table'])) ?> <?= $tt ? 'times' : 'scores' ?></h2>
    <?php if ($new === 0): ?>
        <div class="msg ok">All of these <?= $tt ? 'times' : 'scores' ?> are already on the leaderboard.</div>
    <?php else: ?>
        <form class="submit" method="post" action="<?= h(self_url()) ?>">
            <input type="hidden" name="d" value="<?= h($code) ?>">
            <input type="text" name="username" value="<?= h($name) ?>" placeholder="User name (3–20 letters or numbers)"
                   minlength="3" maxlength="20" pattern="[A-Za-z0-9 _.\-]{3,20}" required autocomplete="nickname">
            <button type="submit">Submit <?= $new ?> <?= $tt ? 'time' : 'score' ?><?= $new === 1 ? '' : 's' ?></button>
        </form>
        <p class="note">No password needed. Your name is shown next to your <?= $tt ? 'times' : 'scores' ?>.</p>
    <?php endif; ?>
    <div class="scroll"><table>
        <thead><tr><th>#</th><?php if (!$tt): ?><th class="num">Score</th><?php endif; ?><th class="hide-sm">Initials</th><th>Route</th><th class="num">Time</th><th></th></tr></thead>
        <tbody>
        <?php foreach ($data['entries'] as $i => $e):
            $done = !empty($known[fingerprint($data['table'], $e)]); ?>
            <tr>
                <td class="rank"><?= $i + 1 ?></td>
                <?php if (!$tt): ?><td class="num score"><?= h(format_score($e['score'])) ?></td><?php endif; ?>
                <td class="initials hide-sm"><?= h($e['initials']) ?></td>
                <td><div class="route"><?= route_svg($e['route']) ?><span><?= h(route_label($e['route'])) ?></span></div></td>
                <td class="num<?= $tt ? ' score' : '' ?>"><?= h(format_time($e['time_cs'])) ?></td>
                <td><?= $done ? '<span class="old">on</span>' : '<span class="new">new</span>' ?></td>
            </tr>
        <?php endforeach; ?>
        </tbody>
    </table></div>
    <?php if (!$tt && !$data['scaled']): ?>
        <p class="note">Difficulty score scaling was off in the game for this table.</p>
    <?php endif; ?>
</div>
<?php
    page_end();
}

// Form posted: store the scores
function handle_submit(): void
{
    $code = (string)($_POST['d'] ?? '');
    $data = lb_decode($code, $err);
    if (!$data) {
        error_page($err);
        return;
    }
    $name = clean_username((string)($_POST['username'] ?? ''));
    if ($name === null) {
        error_page('User names are 3 to 20 characters: letters, numbers, spaces, _ . or -');
        return;
    }

    try {
        $db = lb_db();

        // Nothing new (rescanned table): don't create the name
        $fps = array_map(fn($e) => fingerprint($data['table'], $e), $data['entries']);
        $st  = $db->prepare('SELECT COUNT(*) FROM scores WHERE fingerprint IN (' . implode(',', array_fill(0, count($fps), '?')) . ')');
        $st->execute($fps);
        if ((int)$st->fetchColumn() === count($fps)) {
            header('Location: ' . self_url(['table' => $data['table'], 'added' => 0]), true, 303);
            return;
        }

        $db->beginTransaction();
        $db->prepare('INSERT IGNORE INTO players (username) VALUES (?)')->execute([$name]);
        $st = $db->prepare('SELECT id, username FROM players WHERE username = ?');
        $st->execute([$name]);
        $player = $st->fetch();

        $ins = $db->prepare(
            'INSERT IGNORE INTO scores
                (player_id, score_table, score, initials, route, goal, time_cs, scaled, console_time, fingerprint)
             VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)'
        );
        $added = 0;
        foreach ($data['entries'] as $e) {
            $ins->execute([
                $player['id'], $data['table'], $e['score'], $e['initials'], $e['route'], $e['goal'],
                $e['time_cs'], $data['scaled'], $data['console_time'], fingerprint($data['table'], $e),
            ]);
            $added += $ins->rowCount();
        }
        $db->commit();
    } catch (PDOException $ex) {
        if (isset($db) && $db->inTransaction()) {
            $db->rollBack();
        }
        db_error_page($ex);
        return;
    }

    setcookie(LB_NAME_COOKIE, $player['username'], [
        'expires' => time() + 365 * 86400, 'path' => '/', 'samesite' => 'Lax',
    ]);
    header('Location: ' . self_url([
        'table' => $data['table'], 'player' => $player['username'], 'added' => $added,
    ]), true, 303);
}

function show_leaderboard(): void
{
    $table = (int)($_GET['table'] ?? 0);
    if ($table < 0 || $table > LB_TT_BASE + 3) {
        $table = 0;
    }
    $tt = is_tt($table);
    $filters = route_filters();
    $route = (string)($_GET['route'] ?? 'all');
    if (!isset($filters[$route])) {
        $route = 'all';
    }
    $best = !isset($_GET['all']);           // one row per player unless ?all
    $me = (string)($_GET['player'] ?? '');

    // Filter on the table and route, for s and (for best-per-player) s2.
    // Positional parameters: real prepared statements can't reuse a name.
    $params = [];
    $where = function (string $a) use ($route, $table, &$params): string {
        $w = "$a.score_table = ?";
        $params[] = $table;
        if (strncmp($route, 'goal-', 5) === 0) {
            $w .= " AND $a.goal = ?";
            $params[] = substr($route, 5);
        } elseif (strncmp($route, 'route-', 6) === 0) {
            $w .= " AND $a.route = ?";
            $params[] = (int)substr($route, 6);
        }
        return $w;
    };
    $sql = 'SELECT s.score, s.initials, s.route, s.time_cs, s.scaled, UNIX_TIMESTAMP(s.submitted_at) AS submitted_ts, p.username
            FROM scores s JOIN players p ON p.id = s.player_id
            WHERE ' . $where('s');
    if ($best) {
        // Each player's best: no better (or equal and earlier) entry of theirs.
        // Time trial: lowest time; otherwise highest score.
        $better = $tt ? '(s2.time_cs < s.time_cs OR (s2.time_cs = s.time_cs AND s2.id < s.id))'
                      : '(s2.score > s.score OR (s2.score = s.score AND s2.id < s.id))';
        $sql .= ' AND NOT EXISTS (SELECT 1 FROM scores s2 WHERE s2.player_id = s.player_id AND ' . $where('s2') .
                ' AND ' . $better . ')';
    }
    $sql .= ($tt ? ' ORDER BY s.time_cs ASC, s.id ASC' : ' ORDER BY s.score DESC, s.id ASC') . ' LIMIT ' . (int)LB_ROWS;

    try {
        $st = lb_db()->prepare($sql);
        $st->execute($params);
        $rows = $st->fetchAll();
    } catch (PDOException $ex) {
        db_error_page($ex);
        return;
    }

    page_start(LB_TITLE);
    if (isset($_GET['added'])) {
        $n = (int)$_GET['added'];
        echo '<div class="msg ok">' . ($n
            ? 'Thanks, ' . h($me) . '! ' . $n . ($tt ? ' time' : ' score') . ($n === 1 ? '' : 's') . ' added.'
            : 'Those were already on the leaderboard.') . '</div>';
    }
    ?>
<form class="panel filters" method="get" action="<?= h(self_url()) ?>">
    <label>Table
        <select name="table" onchange="this.form.submit()">
            <?php for ($t = 0; $t < LB_TT_BASE + 4; $t++): ?>
                <option value="<?= $t ?>"<?= $t === $table ? ' selected' : '' ?>><?= h(table_name($t)) ?></option>
            <?php endfor; ?>
        </select>
    </label>
    <label>Route
        <select name="route" onchange="this.form.submit()">
            <?php foreach ($filters as $k => $label): ?>
                <option value="<?= h($k) ?>"<?= $k === $route ? ' selected' : '' ?>><?= h($label) ?></option>
            <?php endforeach; ?>
        </select>
    </label>
    <label class="check"><input type="checkbox" name="all" value="1"<?= $best ? '' : ' checked' ?> onchange="this.form.submit()"> Every score (not just each player's best)</label>
    <?php if ($me !== ''): ?><input type="hidden" name="player" value="<?= h($me) ?>"><?php endif; ?>
    <noscript><button type="submit">Show</button></noscript>
</form>

<div class="panel">
    <h2 style="margin-top:0"><?= h(table_name($table)) ?> · <?= h($filters[$route]) ?></h2>
    <?php if (!$rows): ?>
        <p class="note">Nothing here yet.</p>
    <?php else: ?>
    <div class="scroll"><table>
        <thead><tr>
            <th>#</th><th>Player</th><?php if (!$tt): ?><th class="num">Score</th><?php endif; ?><th class="hide-sm">Initials</th>
            <th>Route</th><th class="num">Time</th><th class="when">Submitted</th>
        </tr></thead>
        <tbody>
        <?php foreach ($rows as $i => $r):
            $cls = ($i === 0 ? 'top1' : '') . (strcasecmp($r['username'], $me) === 0 && $me !== '' ? ' mine' : ''); ?>
            <tr class="<?= trim($cls) ?>">
                <td class="rank"><?= $i + 1 ?></td>
                <td><?= h($r['username']) ?><?= ($tt || $r['scaled']) ? '' : '<span class="tag" title="Score scaling was off">unscaled</span>' ?>
                    <div class="when-sm"><?= submitted_html((int)$r['submitted_ts']) ?></div></td>
                <?php if (!$tt): ?><td class="num score"><?= h(format_score((int)$r['score'])) ?></td><?php endif; ?>
                <td class="initials hide-sm"><?= h($r['initials']) ?></td>
                <td><div class="route"><?= route_svg($r['route'] === null ? null : (int)$r['route']) ?><span><?= h(route_label($r['route'] === null ? null : (int)$r['route'])) ?></span></div></td>
                <td class="num<?= $tt ? ' score' : '' ?>"><?= h(format_time($r['time_cs'] === null ? null : (int)$r['time_cs'])) ?></td>
                <td class="when"><?= submitted_html((int)$r['submitted_ts']) ?></td>
            </tr>
        <?php endforeach; ?>
        </tbody>
    </table></div>
    <script>
    // Submitted times in the viewer's own time zone (the page has them in UTC)
    document.querySelectorAll('time[data-ts]').forEach(function (el) {
        var d = new Date(el.dataset.ts * 1000);
        el.querySelector('.d').textContent = d.toLocaleDateString(undefined, {year: 'numeric', month: 'short', day: 'numeric'});
        el.querySelector('.t').textContent = ' ' + d.toLocaleTimeString(undefined, {hour: 'numeric', minute: '2-digit'});
        el.title = d.toLocaleString();
    });
    </script>
    <?php endif; ?>
</div>
<?php
    page_end();
}

// ---------------------------------------------------------------------------

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'POST') {
    handle_submit();
} elseif (isset($_GET['d'])) {
    show_submit_form((string)$_GET['d']);
} else {
    show_leaderboard();
}
