<?php
/**
 * MW-Aufnahme System - Server-Sent Events Endpoint
 *
 * Sendet alle 2 Sekunden den aktuellen Status aller User an den Browser.
 * Markiert User ohne Heartbeat automatisch als offline.
 */

require_once __DIR__ . '/includes/db.php';
require_dashboard_auth();

/* Session schliessen damit andere Requests nicht blockiert werden */
session_write_close();

/* SSE Headers */
header('Content-Type: text/event-stream');
header('Cache-Control: no-cache');
header('Connection: keep-alive');
header('Access-Control-Allow-Origin: *');
header('X-Accel-Buffering: no'); /* nginx buffering deaktivieren */

/* Verhindere PHP Timeout (AllInkl hat meistens 30s Default) */
set_time_limit(0);

/* Prüfen ob DB erreichbar ist */
try {
    $db = get_db();
} catch (PDOException $e) {
    echo 'data: ' . json_encode([
        'error'        => 'Datenbankfehler: ' . $e->getMessage(),
        'online_count' => 0,
        'sessions'     => [],
        'timestamp'    => date('Y-m-d H:i:s'),
    ]) . "\n\n";
    flush();
    exit;
}

/* Maximale Laufzeit: 5 Minuten, danach reconnected der Browser */
$max_runtime = 300;
$start = time();

while (true) {
    /* Timeout erreicht? Browser reconnected automatisch */
    if (time() - $start > $max_runtime) {
        echo "event: timeout\ndata: reconnect\n\n";
        break;
    }

    try {
        /* Stale User als offline markieren */
        mark_stale_users_offline();

        $db = get_db();

        /* Nur die neueste Session pro User (innerhalb 24h) */
        $stmt = $db->prepare(
            "SELECT s.id, s.user_name, s.camera_id, s.status, s.started_at, s.stopped_at,
                    s.last_heartbeat, s.offset_ms, s.sync_method,
                    s.pending_resync, s.last_recording_active,
                    s.plugin_version, s.offset_age_sec,
                    s.sync_lost_in_session
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

        $data = [
            'online_count' => $online_count,
            'sessions'     => $sessions,
            'timestamp'    => date('Y-m-d H:i:s'),
        ];
    } catch (PDOException $e) {
        $data = [
            'error'        => 'DB: ' . $e->getMessage(),
            'online_count' => 0,
            'sessions'     => [],
            'timestamp'    => date('Y-m-d H:i:s'),
        ];
    }

    echo 'data: ' . json_encode($data, JSON_UNESCAPED_UNICODE) . "\n\n";

    if (ob_get_level() > 0) {
        ob_flush();
    }
    flush();

    /* Verbindung noch aktiv? */
    if (connection_aborted()) {
        break;
    }

    sleep(SSE_INTERVAL);
}
