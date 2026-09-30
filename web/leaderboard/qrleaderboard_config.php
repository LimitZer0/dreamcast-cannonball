<?php
// QRLeaderboard: gives the pass maker (qrleaderboard.html, in this folder)
// this leaderboard's address and key, so players don't type or see them.
// Both come from outrun_config.php; nothing to edit here.
//
// The key still ends up inside every pass (the Dreamcast needs it to sign
// the scores), so this keeps it out of sight, not secret.

require __DIR__ . '/outrun_config.php';

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Robots-Tag: noindex');

// No key yet (still the zeros in outrun_config.php): the page says so
if (!preg_match('/^[0-9a-fA-F]{64}$/', LB_KEY_HEX) || trim(LB_KEY_HEX, '0') === '') {
    http_response_code(503);
    echo json_encode(['error' => 'LB_KEY_HEX is not set up']);
    exit;
}

// The leaderboard's address as players' Dreamcasts should use it. Set
// LB_PUBLIC_URL in outrun_config.php if the one worked out here is wrong
// (e.g. behind a proxy); otherwise outrun_leaderboard.php next to this file.
$url = defined('LB_PUBLIC_URL') ? (string)LB_PUBLIC_URL : '';
if ($url === '') {
    $https = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off')
          || strtolower($_SERVER['HTTP_X_FORWARDED_PROTO'] ?? '') === 'https'
          || ($_SERVER['SERVER_PORT'] ?? '') === '443';
    $host = $_SERVER['HTTP_HOST'] ?? ($_SERVER['SERVER_NAME'] ?? '');
    $dir = rtrim(str_replace('\\', '/', dirname($_SERVER['SCRIPT_NAME'] ?? '/')), '/');
    $url = ($https ? 'https' : 'http') . '://' . $host . $dir . '/outrun_leaderboard.php';
}

echo json_encode(['url' => $url, 'key' => strtolower(LB_KEY_HEX)], JSON_UNESCAPED_SLASHES);
