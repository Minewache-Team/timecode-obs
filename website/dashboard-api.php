<?php
/**
 * MW-Aufnahme System - Dashboard API
 *
 * AJAX-Endpoints fuer das Dashboard (Session-Auth, nur Regisseur).
 *   POST ?action=delete_session   - Session/User entfernen (soft-delete)
 *   POST ?action=force_stop       - Aufnahme erzwungen beenden (online -> offline)
 *   POST ?action=delete_scene     - Szene loeschen
 *   GET  ?action=episode_stats    - Statistik pro Folge
 *   POST ?action=request_resync   - NTP-Resync auf einem Camera-Plugin anfordern
 */

require_once __DIR__ . '/includes/db.php';
require_once __DIR__ . '/includes/latest_version.php';

/* Dispatch-Logik nur ausfuehren wenn nicht im Test-Modus (TICKET-038). */
if (!defined('MW_TEST_MODE') || !MW_TEST_MODE) {
    require_dashboard_auth();

    $action = $_GET['action'] ?? '';

    switch ($action) {
        case 'delete_session':
            handle_delete_session();
            break;
        case 'force_stop':
            handle_force_stop();
            break;
        case 'delete_scene':
            handle_delete_scene();
            break;
        case 'episode_stats':
            handle_episode_stats();
            break;
        case 'request_resync':
            handle_request_resync();
            break;
        case 'set_latest_version':
            handle_set_latest_version();
            break;
        default:
            json_response(['error' => 'Unknown action'], 400);
    }
} /* end !MW_TEST_MODE dispatch */

function handle_delete_session(): void
{
    if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
        json_response(['error' => 'Method not allowed'], 405);
    }

    $input = json_decode(file_get_contents('php://input'), true);
    $session_id = (int) ($input['session_id'] ?? 0);

    if ($session_id < 1) {
        json_response(['error' => 'Invalid session_id'], 400);
    }

    $db = get_db();
    $stmt = $db->prepare(
        "UPDATE sessions SET status = 'removed', stopped_at = COALESCE(stopped_at, NOW()) WHERE id = :id"
    );
    $stmt->execute([':id' => $session_id]);

    json_response([
        'ok'      => true,
        'updated' => $stmt->rowCount(),
        'message' => 'Session entfernt',
    ]);
}

function handle_force_stop(): void
{
    if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
        json_response(['error' => 'Method not allowed'], 405);
    }

    $input = json_decode(file_get_contents('php://input'), true);
    $session_id = (int) ($input['session_id'] ?? 0);

    if ($session_id < 1) {
        json_response(['error' => 'Invalid session_id'], 400);
    }

    $db = get_db();
    $stmt = $db->prepare(
        "UPDATE sessions SET status = 'offline', stopped_at = NOW()
         WHERE id = :id AND status = 'online'"
    );
    $stmt->execute([':id' => $session_id]);

    json_response([
        'ok'      => true,
        'updated' => $stmt->rowCount(),
        'message' => 'Aufnahme beendet',
    ]);
}

function handle_delete_scene(): void
{
    if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
        json_response(['error' => 'Method not allowed'], 405);
    }

    $input = json_decode(file_get_contents('php://input'), true);
    $scene_id = (int) ($input['scene_id'] ?? 0);

    if ($scene_id < 1) {
        json_response(['error' => 'Invalid scene_id'], 400);
    }

    $db = get_db();
    $stmt = $db->prepare("DELETE FROM scenes WHERE id = :id");
    $stmt->execute([':id' => $scene_id]);

    json_response([
        'ok'      => true,
        'deleted' => $stmt->rowCount(),
        'message' => 'Szene geloescht',
    ]);
}

function handle_request_resync(): void
{
    if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
        json_response(['error' => 'Method not allowed'], 405);
    }

    $input = json_decode(file_get_contents('php://input'), true);
    $session_id = (int) ($input['session_id'] ?? 0);

    if ($session_id < 1) {
        json_response(['error' => 'Invalid session_id'], 400);
    }

    $db = get_db();

    /* Existiert die Session noch und ist nicht entfernt? */
    $stmt = $db->prepare(
        "SELECT user_name, status, last_recording_active FROM sessions
          WHERE id = :id AND status != 'removed'"
    );
    $stmt->execute([':id' => $session_id]);
    $row = $stmt->fetch();
    if (!$row) {
        json_response(['error' => 'Session nicht gefunden'], 404);
    }

    /* Resync immer auf die LATESTE Session des Users setzen — wenn z.B.
     * eine neue Aufnahme inzwischen gestartet wurde, soll das Flag auf der
     * neuen Session liegen. handle_start uebernimmt das Flag bereits beim
     * Anlegen, aber dies hier ist die robuste Schreibseite. */
    $stmt = $db->prepare(
        "UPDATE sessions
           SET pending_resync = 1
         WHERE user_name = :name AND status != 'removed'
         ORDER BY id DESC LIMIT 1"
    );
    $stmt->execute([':name' => $row['user_name']]);

    /* Hinweis fuer den Director: Wenn gerade aufgenommen wird, wartet das
     * System bis zum Idle — kein Sofort-Resync waehrend Aufnahme. */
    $message = ((int) $row['last_recording_active'] === 1)
        ? 'Resync angefragt — wird nach Aufnahmestop ausgefuehrt'
        : 'Resync angefragt — wird beim naechsten Heartbeat ausgefuehrt';

    json_response([
        'ok'                    => true,
        'queued_during_recording' => (int) $row['last_recording_active'] === 1,
        'message'               => $message,
    ]);
}

/* TICKET-060: Dashboard-UI zum Setzen der "Latest known Plugin-Version".
 * Schreibt in website/.latest_version_override — wird von
 * latest_version.php gelesen und hat Vorrang vor GitHub-Fetch und Cache.
 * Auth: erbt require_dashboard_auth() vom dispatch oben. */
function handle_set_latest_version(): void
{
    if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
        json_response(['error' => 'Method not allowed'], 405);
    }

    $input = json_decode(file_get_contents('php://input'), true);
    $version = isset($input['version']) && is_string($input['version'])
        ? trim($input['version']) : '';

    if (!preg_match('/^[\w.+-]{1,20}$/', $version)) {
        json_response(['error' => 'Invalid version format (must match ^[\\w.+-]{1,20}$)'], 400);
    }

    $path = _mw_latest_version_override_path();
    if (@file_put_contents($path, $version) === false) {
        json_response(['error' => 'Could not write override file at ' . basename($path)], 500);
    }

    /* Cache invalidieren falls vorhanden — der Override soll sofort
     * sichtbar sein, nicht erst nach Cache-TTL. */
    $cache = _mw_latest_version_cache_path();
    if (file_exists($cache)) {
        @unlink($cache);
    }

    json_response([
        'ok'      => true,
        'version' => $version,
        'message' => 'Latest Plugin-Version gesetzt: ' . $version,
    ]);
}

function handle_episode_stats(): void
{
    $db = get_db();

    /* Alle Szenen gruppiert nach Staffel+Folge */
    $stmt = $db->query(
        "SELECT season, episode, GROUP_CONCAT(session_ids) as all_session_ids,
                COUNT(*) as scene_count,
                MIN(started_at) as first_start,
                MAX(stopped_at) as last_stop
         FROM scenes
         GROUP BY season, episode
         ORDER BY season ASC, episode ASC"
    );
    $episodes = $stmt->fetchAll();

    /* Fuer jede Folge: alle beteiligten User aus den Sessions laden */
    $result = [];
    foreach ($episodes as $ep) {
        $all_ids = [];
        if (!empty($ep['all_session_ids'])) {
            foreach (explode(',', $ep['all_session_ids']) as $id) {
                $id = (int) trim($id);
                if ($id > 0) $all_ids[$id] = true;
            }
        }

        $participants = [];
        if (!empty($all_ids)) {
            $ids = array_keys($all_ids);
            $placeholders = implode(',', array_fill(0, count($ids), '?'));
            $s = $db->prepare(
                "SELECT DISTINCT user_name, camera_id FROM sessions WHERE id IN ($placeholders)"
            );
            $s->execute($ids);
            $participants = $s->fetchAll();
        }

        $result[] = [
            'season'       => (int) $ep['season'],
            'episode'      => (int) $ep['episode'],
            'scene_count'  => (int) $ep['scene_count'],
            'first_start'  => $ep['first_start'],
            'last_stop'    => $ep['last_stop'],
            'participants' => $participants,
        ];
    }

    json_response(['ok' => true, 'episodes' => $result]);
}
