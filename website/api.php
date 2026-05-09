<?php
/**
 * MW-Aufnahme System - REST API
 *
 * Endpoints:
 *   POST ?action=start     - Aufnahme starten (User geht online)
 *   POST ?action=stop      - Aufnahme stoppen (User geht offline)
 *   POST ?action=heartbeat - Heartbeat senden
 *   GET  ?action=status    - Aktueller Status aller User
 *   POST ?action=consent   - DSGVO-Einwilligung loggen
 */

require_once __DIR__ . '/includes/db.php';

/* CORS Headers für Preflight */
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    header('Access-Control-Allow-Origin: *');
    header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
    header('Access-Control-Allow-Headers: Content-Type, X-API-Key');
    http_response_code(204);
    exit;
}

$action = $_GET['action'] ?? '';

/* Status-Endpoint braucht keinen API-Key (öffentlich für Dashboard) */
if ($action === 'status') {
    handle_status();
}

/* Alle anderen Endpoints brauchen API-Key */
if (!validate_api_key()) {
    json_response(['error' => 'Unauthorized'], 401);
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    json_response(['error' => 'Method not allowed'], 405);
}

$input = json_decode(file_get_contents('php://input'), true);
if (!is_array($input)) {
    json_response(['error' => 'Invalid JSON body'], 400);
}

switch ($action) {
    case 'start':
        handle_start($input);
        break;
    case 'stop':
        handle_stop($input);
        break;
    case 'heartbeat':
        handle_heartbeat($input);
        break;
    case 'consent':
        handle_consent($input);
        break;
    default:
        json_response(['error' => 'Unknown action'], 400);
}

/* ---- Handlers ---- */

function handle_start(array $input): void
{
    $name = sanitize_name($input['name'] ?? '');
    $camera = strtoupper(trim($input['camera_id'] ?? ''));

    if ($name === '') {
        json_response(['error' => 'Name is required'], 400);
    }
    if (!validate_camera_id($camera)) {
        json_response(['error' => 'Invalid camera_id (A-P)'], 400);
    }

    $db = get_db();

    /* Falls der User bereits online ist, zuerst alte Session schließen */
    $stmt = $db->prepare(
        "UPDATE sessions SET status = 'offline', stopped_at = NOW()
         WHERE user_name = :name AND status = 'online'"
    );
    $stmt->execute([':name' => $name]);

    /* Neue Session anlegen */
    $stmt = $db->prepare(
        "INSERT INTO sessions (user_name, camera_id, status, started_at, last_heartbeat)
         VALUES (:name, :camera, 'online', NOW(), NOW())"
    );
    $stmt->execute([':name' => $name, ':camera' => $camera]);

    json_response([
        'ok'         => true,
        'session_id' => (int) $db->lastInsertId(),
        'message'    => 'Recording started',
    ]);
}

function handle_stop(array $input): void
{
    $name = sanitize_name($input['name'] ?? '');
    $camera = strtoupper(trim($input['camera_id'] ?? ''));

    if ($name === '') {
        json_response(['error' => 'Name is required'], 400);
    }

    $db = get_db();
    $stmt = $db->prepare(
        "UPDATE sessions SET status = 'offline', stopped_at = NOW()
         WHERE user_name = :name AND status = 'online'"
    );
    $stmt->execute([':name' => $name]);

    json_response([
        'ok'      => true,
        'message' => 'Recording stopped',
        'updated' => $stmt->rowCount(),
    ]);
}

function handle_heartbeat(array $input): void
{
    $name = sanitize_name($input['name'] ?? '');

    if ($name === '') {
        json_response(['error' => 'Name is required'], 400);
    }

    $db = get_db();
    $stmt = $db->prepare(
        "UPDATE sessions SET last_heartbeat = NOW()
         WHERE user_name = :name AND status = 'online'"
    );
    $stmt->execute([':name' => $name]);

    json_response([
        'ok'      => true,
        'updated' => $stmt->rowCount(),
    ]);
}

function handle_status(): void
{
    $db = get_db();

    /* Stale User als offline markieren */
    mark_stale_users_offline();

    /* Nur die neueste Session pro User (innerhalb 24h) */
    $stmt = $db->prepare(
        "SELECT s.id, s.user_name, s.camera_id, s.status, s.started_at, s.stopped_at, s.last_heartbeat
         FROM sessions s
         INNER JOIN (
             SELECT user_name, MAX(id) as max_id
             FROM sessions
             WHERE created_at > DATE_SUB(NOW(), INTERVAL 24 HOUR)
               AND status != 'removed'
             GROUP BY user_name
         ) latest ON s.id = latest.max_id
         ORDER BY s.status DESC, s.started_at DESC"
    );
    $stmt->execute();

    $sessions = $stmt->fetchAll();
    $online_count = 0;
    foreach ($sessions as $s) {
        if ($s['status'] === 'online') {
            $online_count++;
        }
    }

    json_response([
        'ok'           => true,
        'online_count' => $online_count,
        'sessions'     => $sessions,
        'timestamp'    => date('Y-m-d H:i:s'),
    ]);
}

function handle_consent(array $input): void
{
    $name = sanitize_name($input['name'] ?? '');
    $consent = (bool) ($input['consent'] ?? false);

    if ($name === '') {
        json_response(['error' => 'Name is required'], 400);
    }

    $db = get_db();
    $stmt = $db->prepare(
        "INSERT INTO consent_log (user_name, consent_given, ip_address)
         VALUES (:name, :consent, :ip)"
    );
    $stmt->execute([
        ':name'    => $name,
        ':consent' => $consent ? 1 : 0,
        ':ip'      => $_SERVER['REMOTE_ADDR'] ?? '',
    ]);

    json_response([
        'ok'      => true,
        'message' => 'Consent logged',
    ]);
}
