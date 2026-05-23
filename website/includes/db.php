<?php
/**
 * MW-Aufnahme System - Datenbankverbindung (PDO)
 */

/* Im Test-Modus laedt bootstrap.php config-test.php bereits VOR diesem
 * Require — also nicht erneut config.php anziehen wenn die Konstanten
 * schon definiert sind (config.php ist in Production gitignored, im Test
 * existiert sie typischerweise nicht). */
if (!defined('DB_HOST')) {
    require_once __DIR__ . '/config.php';
}

function get_db(): PDO
{
    static $pdo = null;
    if ($pdo !== null) {
        return $pdo;
    }

    $dsn = 'mysql:host=' . DB_HOST
         . (defined('DB_PORT') ? ';port=' . DB_PORT : '')
         . ';dbname=' . DB_NAME
         . ';charset=' . DB_CHARSET;

    $pdo = new PDO($dsn, DB_USER, DB_PASS, [
        PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
        PDO::ATTR_EMULATE_PREPARES   => false,
        /* "Matched rows" Semantik statt "changed rows". Sonst gibt
         * rowCount() 0 zurueck, wenn ein UPDATE-Statement zwar zeilen
         * matched aber keine Werte geaendert hat (z.B. idempotenter
         * Heartbeat). handle_heartbeat() braucht den korrekten Wert um
         * zu entscheiden ob eine Session ueberhaupt existierte. */
        PDO::MYSQL_ATTR_FOUND_ROWS   => true,
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
 *
 * Im Test-Modus (Konstante MW_TEST_MODE definiert) wird stattdessen eine
 * JsonResponseException geworfen, damit PHPUnit-Tests die Antwort
 * inspizieren koennen ohne dass exit() den Prozess beendet.
 */
class JsonResponseException extends \Exception
{
    public int $http_status;
    public array $data;
    public function __construct(int $status, array $data)
    {
        parent::__construct("json_response: HTTP $status");
        $this->http_status = $status;
        $this->data = $data;
    }
}

function json_response(array $data, int $status = 200): void
{
    if (defined('MW_TEST_MODE') && MW_TEST_MODE) {
        throw new JsonResponseException($status, $data);
    }
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    header('Access-Control-Allow-Origin: *');
    echo json_encode($data, JSON_UNESCAPED_UNICODE);
    exit;
}

/**
 * Prüft ob der Regisseur eingeloggt ist (Discord OAuth2, Session-basiert).
 * Leitet zur Login-Seite weiter wenn nicht authentifiziert.
 */
function require_dashboard_auth(): void
{
    /* In test mode: erlaubt explizites Setzen der Session-Variable, ohne
     * dass eine echte PHP-Session gestartet wird. Tests koennen MW_TEST_AUTH
     * setzen um den Authenticated-Zustand zu simulieren. */
    if (defined('MW_TEST_MODE') && MW_TEST_MODE) {
        if (empty($_SESSION['mw_authenticated'])) {
            throw new JsonResponseException(401, ['error' => 'Unauthorized (test)']);
        }
        return;
    }

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
