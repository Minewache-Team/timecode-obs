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

/* Dispatch-Logik nur ausfuehren wenn nicht im Test-Modus (TICKET-038).
 * PHPUnit-Tests setzen MW_TEST_MODE und rufen die handle_*-Funktionen
 * direkt auf, um die SQL-Vertraege zu pruefen ohne HTTP-Roundtrip. */
if (!defined('MW_TEST_MODE') || !MW_TEST_MODE) {

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

} /* end !MW_TEST_MODE dispatch */

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

    /* Pending Resync-Flag der letzten Session uebernehmen (TICKET-036).
     * Verhindert dass Director-Resync-Anfragen verloren gehen, wenn der
     * User zwischen Klick und Lieferung eine neue Aufnahme startet. */
    $stmt = $db->prepare(
        "SELECT pending_resync FROM sessions
          WHERE user_name = :name AND status != 'removed'
          ORDER BY id DESC LIMIT 1"
    );
    $stmt->execute([':name' => $name]);
    $prev_pending = (int) ($stmt->fetchColumn() ?: 0);

    /* Falls der User bereits online ist, zuerst alte Session schließen */
    $stmt = $db->prepare(
        "UPDATE sessions SET status = 'offline', stopped_at = NOW()
         WHERE user_name = :name AND status = 'online'"
    );
    $stmt->execute([':name' => $name]);

    /* Neue Session anlegen — last_recording_active=1, weil eine Aufnahme
     * gerade startet. Ggf. uebernommenes pending_resync wird beim naechsten
     * Idle-Heartbeat (nach Stop) ausgeliefert. */
    $stmt = $db->prepare(
        "INSERT INTO sessions (user_name, camera_id, status, started_at, last_heartbeat, pending_resync, last_recording_active)
         VALUES (:name, :camera, 'online', NOW(), NOW(), :pending, 1)"
    );
    $stmt->execute([
        ':name'    => $name,
        ':camera'  => $camera,
        ':pending' => $prev_pending,
    ]);

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

    /* Offset/Sync-Method aus dem Plugin (TICKET-035).
     * Beide Felder sind optional fuer Abwaertskompatibilitaet mit alten Plugins. */
    $offset_ms = null;
    if (isset($input['offset_ms']) && is_numeric($input['offset_ms'])) {
        $val = (int) $input['offset_ms'];
        /* Plausibilitaetsbereich: +/- 1 Tag */
        if ($val >= -86400000 && $val <= 86400000) {
            $offset_ms = $val;
        }
    }
    $sync_method = null;
    if (isset($input['sync_method']) && is_numeric($input['sync_method'])) {
        $val = (int) $input['sync_method'];
        if ($val >= 0 && $val <= 3) {
            $sync_method = $val;
        }
    }

    /* recording_active aus dem Plugin (TICKET-036).
     * Wenn nicht mitgeschickt (alte Plugins): true annehmen, also keine
     * idle-Resync-Auslieferung — defensiv. */
    $recording_active = isset($input['recording_active'])
        ? (bool) $input['recording_active'] : true;

    $db = get_db();

    /* Update der LATESTEN Session des Users — auch wenn offline.
     * Das macht Idle-Heartbeats zwischen Aufnahmen sichtbar und gibt uns
     * einen Kanal um Resync-Kommandos im idle auszuliefern. */
    $stmt = $db->prepare(
        "UPDATE sessions
           SET last_heartbeat        = NOW(),
               offset_ms             = :offset,
               sync_method           = :method,
               last_recording_active = :rec
         WHERE user_name = :name AND status != 'removed'
         ORDER BY id DESC
         LIMIT 1"
    );
    $stmt->execute([
        ':name'   => $name,
        ':offset' => $offset_ms,
        ':method' => $sync_method,
        ':rec'    => $recording_active ? 1 : 0,
    ]);
    $updated_rows = $stmt->rowCount();

    /* Resync-Kommando nur ausliefern, wenn das Plugin gerade NICHT aufnimmt.
     * Hard-Resync waehrend Aufnahme wuerde den Timecode zerstoeren — siehe
     * TICKET-008 Decision Log. Flag bleibt gesetzt bis sicher geliefert. */
    $deliver_resync = false;
    if (!$recording_active && $updated_rows > 0) {
        $stmt = $db->prepare(
            "SELECT id, pending_resync FROM sessions
              WHERE user_name = :name AND status != 'removed'
              ORDER BY id DESC LIMIT 1"
        );
        $stmt->execute([':name' => $name]);
        $row = $stmt->fetch();
        if ($row && (int) $row['pending_resync'] === 1) {
            /* Flag loeschen und im Response ausliefern */
            $clear = $db->prepare(
                "UPDATE sessions SET pending_resync = 0 WHERE id = :id"
            );
            $clear->execute([':id' => $row['id']]);
            $deliver_resync = true;
        }
    }

    $response = [
        'ok'      => true,
        'updated' => $updated_rows,
    ];
    if ($deliver_resync) {
        $response['resync'] = true;
    }
    json_response($response);
}

function handle_status(): void
{
    $db = get_db();

    /* Stale User als offline markieren */
    mark_stale_users_offline();

    /* Nur die neueste Session pro User (innerhalb 24h) */
    $stmt = $db->prepare(
        "SELECT s.id, s.user_name, s.camera_id, s.status, s.started_at, s.stopped_at,
                s.last_heartbeat, s.offset_ms, s.sync_method,
                s.pending_resync, s.last_recording_active
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
