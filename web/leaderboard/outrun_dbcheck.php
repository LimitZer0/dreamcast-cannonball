<?php
// Leaderboard setup check. Upload next to outrun_config.php, open it in a
// browser, fix what it reports, then DELETE this file from the server.
header('Content-Type: text/plain; charset=utf-8');

echo "PHP version: " . PHP_VERSION . (version_compare(PHP_VERSION, '7.4', '>=') ? "  OK\n" : "  TOO OLD (needs 7.4+)\n");
echo "PDO MySQL driver: " . (extension_loaded('pdo_mysql') ? "OK\n" : "MISSING (enable pdo_mysql in your host's PHP settings)\n");

$cfg = __DIR__ . '/outrun_config.php';
if (!is_file($cfg)) {
    exit("outrun_config.php: NOT FOUND in " . __DIR__ . "\n");
}
require $cfg;
echo "outrun_config.php: found\n";
echo "  host     = " . LB_DB_HOST . "\n";
echo "  database = " . LB_DB_NAME . "\n";
echo "  user     = " . LB_DB_USER . "\n";
echo "  password = " . (LB_DB_PASS === 'change-me' ? "still 'change-me' (not set!)" : str_repeat('*', strlen(LB_DB_PASS))) . "\n";

if (!extension_loaded('pdo_mysql')) {
    exit;
}

// 1. Log in without choosing a database
try {
    $pdo = new PDO('mysql:host=' . LB_DB_HOST . ';charset=utf8mb4', LB_DB_USER, LB_DB_PASS,
                   [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);
    echo "\nLogin: OK\n";
} catch (PDOException $e) {
    exit("\nLogin: FAILED\n  " . $e->getMessage() . "\n  -> check host, user name and password (hosts often prefix user names, e.g. account_outrun)\n");
}

// 2. Which databases this user can see
$dbs = $pdo->query('SHOW DATABASES')->fetchAll(PDO::FETCH_COLUMN);
echo "Databases this user can see: " . implode(', ', $dbs) . "\n";
if (!in_array(LB_DB_NAME, $dbs, true)) {
    exit("  -> '" . LB_DB_NAME . "' is not one of them. Put the exact name from the list in LB_DB_NAME,\n"
       . "     or add this user to the database in your hosting control panel.\n");
}

// 3. Tables
$pdo->exec('USE `' . str_replace('`', '', LB_DB_NAME) . '`');
$tables = $pdo->query('SHOW TABLES')->fetchAll(PDO::FETCH_COLUMN);
echo "Tables in " . LB_DB_NAME . ": " . ($tables ? implode(', ', $tables) : '(none)') . "\n";
foreach (['players', 'scores'] as $t) {
    if (!in_array($t, $tables, true)) {
        exit("  -> table '$t' is missing. Run the CREATE TABLE statements from schema.sql in this database\n"
           . "     (in phpMyAdmin: select this database first, then the SQL tab).\n");
    }
}

// 4. Reading works
try {
    $n = $pdo->query('SELECT COUNT(*) FROM scores')->fetchColumn();
    echo "Scores stored: $n\n";
} catch (PDOException $e) {
    exit("Reading scores: FAILED\n  " . $e->getMessage() . "\n  -> give the user SELECT and INSERT rights on this database\n");
}

echo "\nEverything looks fine. Delete this file from the server.\n";
