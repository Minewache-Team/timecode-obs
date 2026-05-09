<?php
/**
 * MW-Aufnahme System - Szene speichern
 *
 * Nimmt POST-Daten entgegen und speichert eine Szene in der Datenbank.
 */

require_once __DIR__ . '/includes/db.php';
require_dashboard_auth();

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    json_response(['error' => 'Method not allowed'], 405);
}

$season     = (int) ($_POST['season'] ?? 0);
$episode    = (int) ($_POST['episode'] ?? 0);
$scene_name = trim($_POST['scene_name'] ?? '');
$take       = (int) ($_POST['take'] ?? 1);
$notes      = trim($_POST['notes'] ?? '');

/* Session-IDs der letzten abgeschlossenen Aufnahmen */
$session_ids = trim($_POST['session_ids'] ?? '');

if ($season < 1) {
    json_response(['error' => 'Staffel muss mindestens 1 sein'], 400);
}
if ($episode < 1) {
    json_response(['error' => 'Folge muss mindestens 1 sein'], 400);
}
if ($take < 1) {
    $take = 1;
}

/* Eingaben bereinigen */
$scene_name = mb_substr($scene_name, 0, 255, 'UTF-8');
$scene_name = htmlspecialchars($scene_name, ENT_QUOTES, 'UTF-8');
$notes = mb_substr($notes, 0, 1000, 'UTF-8');

$db = get_db();

/* Zeitraum der zugehörigen Sessions ermitteln */
$started_at = null;
$stopped_at = null;

if ($session_ids !== '') {
    /* Nur numerische IDs erlauben */
    $ids = array_filter(array_map('intval', explode(',', $session_ids)));
    if (!empty($ids)) {
        $placeholders = implode(',', array_fill(0, count($ids), '?'));
        $stmt = $db->prepare(
            "SELECT MIN(started_at) as min_start, MAX(stopped_at) as max_stop
             FROM sessions WHERE id IN ($placeholders)"
        );
        $stmt->execute($ids);
        $row = $stmt->fetch();
        $started_at = $row['min_start'];
        $stopped_at = $row['max_stop'];
    }
}

$stmt = $db->prepare(
    "INSERT INTO scenes (season, episode, scene_name, take, session_ids, started_at, stopped_at, notes)
     VALUES (:season, :episode, :scene_name, :take, :session_ids, :started_at, :stopped_at, :notes)"
);
$stmt->execute([
    ':season'      => $season,
    ':episode'     => $episode,
    ':scene_name'  => $scene_name,
    ':take'        => $take,
    ':session_ids' => $session_ids,
    ':started_at'  => $started_at,
    ':stopped_at'  => $stopped_at,
    ':notes'       => $notes,
]);

json_response([
    'ok'       => true,
    'scene_id' => (int) $db->lastInsertId(),
    'message'  => 'Szene gespeichert',
]);
