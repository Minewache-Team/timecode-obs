<?php
/**
 * Datenbank-Verbindungstest
 * Einmal im Browser aufrufen, dann LÖSCHEN!
 */

require_once __DIR__ . '/includes/config.php';

echo "<h2>MW Aufnahme - DB Verbindungstest</h2>";

echo "<p><strong>Host:</strong> " . DB_HOST . "</p>";
echo "<p><strong>Datenbank:</strong> " . DB_NAME . "</p>";
echo "<p><strong>User:</strong> " . DB_USER . "</p>";

try {
    $dsn = 'mysql:host=' . DB_HOST . ';dbname=' . DB_NAME . ';charset=' . DB_CHARSET;
    $pdo = new PDO($dsn, DB_USER, DB_PASS, [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_TIMEOUT => 5,
    ]);

    $version = $pdo->query('SELECT VERSION()')->fetchColumn();
    echo "<p style='color:green;font-size:1.2em;'><strong>Verbindung erfolgreich!</strong></p>";
    echo "<p>MySQL/MariaDB Version: " . htmlspecialchars($version) . "</p>";

    echo "<p style='color:red;'><strong>WICHTIG: Diese Datei jetzt löschen!</strong></p>";
} catch (PDOException $e) {
    echo "<p style='color:red;font-size:1.2em;'><strong>Verbindung fehlgeschlagen!</strong></p>";
    echo "<p>Fehler: " . htmlspecialchars($e->getMessage()) . "</p>";
    echo "<p>Prüfe Host, Datenbankname, Benutzername und Passwort in <code>includes/config.php</code>.</p>";
}
