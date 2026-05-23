<?php
/**
 * MW-Aufnahme System - Datenbank-Installation
 *
 * Dieses Skript einmalig im Browser aufrufen, um die Tabellen anzulegen.
 * Danach diese Datei löschen oder umbenennen!
 */

require_once __DIR__ . '/includes/db.php';

try {
    $db = get_db();

    $db->exec("
        CREATE TABLE IF NOT EXISTS sessions (
            id INT AUTO_INCREMENT PRIMARY KEY,
            user_name VARCHAR(100) NOT NULL,
            camera_id CHAR(1) NOT NULL,
            status ENUM('online', 'offline', 'removed') DEFAULT 'online',
            started_at DATETIME NOT NULL,
            stopped_at DATETIME NULL,
            last_heartbeat DATETIME NOT NULL,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_status (status),
            INDEX idx_heartbeat (last_heartbeat)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    ");

    $db->exec("
        CREATE TABLE IF NOT EXISTS scenes (
            id INT AUTO_INCREMENT PRIMARY KEY,
            season INT NOT NULL,
            episode INT NOT NULL,
            scene_name VARCHAR(255) DEFAULT '',
            take INT NOT NULL DEFAULT 1,
            session_ids TEXT,
            started_at DATETIME,
            stopped_at DATETIME,
            notes TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_season_episode (season, episode)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    ");

    $db->exec("
        CREATE TABLE IF NOT EXISTS consent_log (
            id INT AUTO_INCREMENT PRIMARY KEY,
            user_name VARCHAR(100) NOT NULL,
            consent_given BOOLEAN NOT NULL,
            ip_address VARCHAR(45),
            consented_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_user (user_name)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    ");

    /* Bestehende Tabelle updaten: 'removed' Status hinzufuegen */
    try {
        $db->exec("ALTER TABLE sessions MODIFY COLUMN status ENUM('online', 'offline', 'removed') DEFAULT 'online'");
    } catch (PDOException $e) {
        /* Ignorieren wenn bereits aktuell */
    }

    /* Bestehende scenes-Tabelle updaten: scene_name optional, take-Spalte */
    try {
        $db->exec("ALTER TABLE scenes MODIFY COLUMN scene_name VARCHAR(255) DEFAULT ''");
    } catch (PDOException $e) { /* bereits aktuell */ }
    try {
        $db->exec("ALTER TABLE scenes ADD COLUMN take INT NOT NULL DEFAULT 1 AFTER scene_name");
    } catch (PDOException $e) { /* bereits vorhanden */ }

    /* Epic 15: Offset-Tracking pro Session (TICKET-035) */
    try {
        $db->exec("ALTER TABLE sessions ADD COLUMN offset_ms INT NULL AFTER last_heartbeat");
    } catch (PDOException $e) { /* bereits vorhanden */ }
    try {
        $db->exec("ALTER TABLE sessions ADD COLUMN sync_method TINYINT NULL AFTER offset_ms");
    } catch (PDOException $e) { /* bereits vorhanden */ }

    /* Epic 15: Remote Re-Sync Flags (TICKET-036) — Resync nur ausserhalb
     * der Aufnahme zulaessig; Flag bleibt gesetzt bis sicher geliefert. */
    try {
        $db->exec("ALTER TABLE sessions ADD COLUMN pending_resync TINYINT(1) NOT NULL DEFAULT 0 AFTER sync_method");
    } catch (PDOException $e) { /* bereits vorhanden */ }
    try {
        $db->exec("ALTER TABLE sessions ADD COLUMN last_recording_active TINYINT(1) NOT NULL DEFAULT 0 AFTER pending_resync");
    } catch (PDOException $e) { /* bereits vorhanden */ }

    /* Epic 18: Plugin-Version pro Session (TICKET-047). Identifiziert
     * veraltete Plugin-Installationen im Regisseur-Panel. */
    try {
        $db->exec("ALTER TABLE sessions ADD COLUMN plugin_version VARCHAR(20) NULL AFTER last_recording_active");
    } catch (PDOException $e) { /* bereits vorhanden */ }

    /* Epic 18: NTP-Sync-Staleness pro Session (TICKET-051). Sekunden seit
     * dem letzten erfolgreichen NTP-Sync — vom Plugin im Heartbeat
     * gemeldet, vom Dashboard als "Letzte Sync: vor X" gerendert. -1 = nie
     * gesynct. */
    try {
        $db->exec("ALTER TABLE sessions ADD COLUMN offset_age_sec INT NULL AFTER plugin_version");
    } catch (PDOException $e) { /* bereits vorhanden */ }

    /* Epic 18: Sync-loss-during-recording sticky flag (TICKET-043). 1 = das
     * Plugin hat zu irgendeinem Zeitpunkt in dieser Plugin-Lifetime
     * 'aufnehmend + 3 NTP-Fails hintereinander' beobachtet. Dashboard
     * rendert das als roten persistenten Marker am User-Card. */
    try {
        $db->exec("ALTER TABLE sessions ADD COLUMN sync_lost_in_session TINYINT(1) NOT NULL DEFAULT 0 AFTER offset_age_sec");
    } catch (PDOException $e) { /* bereits vorhanden */ }

    echo "<h1>Installation erfolgreich!</h1>";
    echo "<p>Alle 3 Tabellen wurden angelegt:</p>";
    echo "<ul>";
    echo "<li><strong>sessions</strong> - Aufnahme-Sessions</li>";
    echo "<li><strong>scenes</strong> - Szenen-Dokumentation</li>";
    echo "<li><strong>consent_log</strong> - DSGVO-Einwilligungen</li>";
    echo "</ul>";
    echo "<p style='color:red;'><strong>WICHTIG:</strong> Lösche oder benenne diese Datei um!</p>";

} catch (PDOException $e) {
    http_response_code(500);
    echo "<h1>Fehler bei der Installation</h1>";
    echo "<p>" . htmlspecialchars($e->getMessage()) . "</p>";
    echo "<p>Prüfe die Einstellungen in <code>includes/config.php</code>.</p>";
}
