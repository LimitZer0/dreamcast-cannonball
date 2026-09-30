-- CannonBall (OutRun) Dreamcast leaderboard: MySQL 5.7+ / MariaDB 10.2+
--
--   mysql -u root -p < schema.sql
--
-- Then create a user for the website (change the password), e.g.:
--   CREATE USER 'outrun'@'localhost' IDENTIFIED BY 'change-me';
--   GRANT SELECT, INSERT ON outrun_leaderboard.* TO 'outrun'@'localhost';
-- On shared hosting, create the database and user in your control panel
-- instead and run only the CREATE TABLE statements below in phpMyAdmin.

CREATE DATABASE IF NOT EXISTS outrun_leaderboard
    CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE outrun_leaderboard;

-- One row per user name. No passwords: a name is claimed by the first
-- submission that uses it. Names compare case-insensitively.
CREATE TABLE IF NOT EXISTS players (
    id          INT UNSIGNED NOT NULL AUTO_INCREMENT,
    username    VARCHAR(20)  NOT NULL,
    created_at  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uq_players_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- One row per high score table entry.
CREATE TABLE IF NOT EXISTS scores (
    id           INT UNSIGNED     NOT NULL AUTO_INCREMENT,
    player_id    INT UNSIGNED     NOT NULL,
    -- Which of the game's 8 high score tables (as on the high score screen):
    -- bit 0 = Japanese tracks, bit 1 = continuous mode, bit 2 = modified
    -- (grippy tyres, off-road, bumper, turbo or timer off).
    -- 8-11 = time trial (full course, no traffic): 8 + Japan (1) + modified (2);
    -- those rows have score 0 and rank by time_cs
    score_table  TINYINT UNSIGNED NOT NULL,
    score        INT UNSIGNED     NOT NULL,
    initials     CHAR(3)          NOT NULL,
    -- Route on the course map: 0 = stage 1, 1-2 stage 2, 3-6 stage 3,
    -- 7-14 stage 4, 15-30 stage 5 (the 16 complete routes). NULL = unknown
    route        TINYINT UNSIGNED NULL,
    -- Goal A-E for a completed route, NULL otherwise
    goal         CHAR(1)          NULL,
    -- Total time as shown in the game, in 1/100 s. NULL = not finished
    time_cs      INT UNSIGNED     NULL,
    -- 1 if the game's difficulty score scaling was on
    scaled       TINYINT(1)       NOT NULL DEFAULT 1,
    -- Console clock when the QR code was made (may be wrong if the
    -- Dreamcast's clock was never set)
    console_time DATETIME         NULL,
    submitted_at DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP,
    -- SHA-256 of the entry itself: the same entry can't be stored twice
    -- (rescanning a table only adds the new entries)
    fingerprint  BINARY(32)       NOT NULL,
    PRIMARY KEY (id),
    UNIQUE KEY uq_scores_fingerprint (fingerprint),
    KEY ix_scores_table_score (score_table, score),
    KEY ix_scores_table_route (score_table, route, score),
    KEY ix_scores_table_goal (score_table, goal, score),
    KEY ix_scores_player (player_id),
    CONSTRAINT fk_scores_player FOREIGN KEY (player_id) REFERENCES players (id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
