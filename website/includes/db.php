<?php
/**
 * MW-Aufnahme System - Datenbankverbindung (PDO)
 */

require_once __DIR__ . '/config.php';

function get_db(): PDO
{
    static $pdo = null;
    if ($pdo !== null) {
        return $pdo;
    }

    $dsn = 'mysql:host=' . DB_HOST
         . ';dbname=' . DB_NAME
         . ';charset=' . DB_CHARSET;

    $pdo = new PDO($dsn, DB_USER, DB_PASS, [
        PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
        PDO::ATTR_EMULATE_PREPARES   => false,
    ]);

    return $pdo;
}

/**
 * Validiert einen API-Key aus dem Request-Header.
 */
function validate_api_key(): bool
{
    $key = $_SERVER['HTTP_X_API_KEY'] ?? '';
    return hash_equals(API_KEY, $key);
}

/**
 * Bereinigt einen Benutzernamen (nur sichere Zeichen).
 */
function sanitize_name(string $name): string
{
    $name = trim($name);
    $name = mb_substr($name, 0, MAX_NAME_LENGTH, 'UTF-8');
    /* Erlaube Buchstaben, Zahlen, Leerzeichen, Bindestriche, Unterstriche */
    $name = preg_replace('/[^\p{L}\p{N}\s\-_]/u', '', $name);
    return $name;
}

/**
 * Validiert eine Kamera-ID (A-P, 16 Werte).
 */
function validate_camera_id(string $id): bool
{
    return preg_match('/^[A-P]$/', $id) === 1;
}

/**
 * Sendet eine JSON-Antwort und beendet das Skript.
 */
function json_response(array $data, int $status = 200): void
{
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    header('Access-Control-Allow-Origin: *');
    echo json_encode($data, JSON_UNESCAPED_UNICODE);
    exit;
}

/**
 * Prüft ob der Regisseur eingeloggt ist (Session-basiert).
 * Leitet zur Login-Seite weiter wenn nicht authentifiziert.
 */
function require_dashboard_auth(): void
{
    if (session_status() === PHP_SESSION_NONE) {
        session_start();
    }

    if (empty($_SESSION['mw_authenticated'])) {
        header('Location: login.php');
        exit;
    }
}

/**
 * Markiert User ohne Heartbeat als offline.
 */
function mark_stale_users_offline(): int
{
    $db = get_db();
    $stmt = $db->prepare(
        "UPDATE sessions
         SET status = 'offline', stopped_at = NOW()
         WHERE status = 'online'
           AND last_heartbeat < DATE_SUB(NOW(), INTERVAL :timeout SECOND)"
    );
    $stmt->execute([':timeout' => HEARTBEAT_TIMEOUT]);
    return $stmt->rowCount();
}
