<?php
/**
 * Tests fuer die Resync-Mechanik (TICKET-038).
 *
 * Verifiziert den Vertrag:
 *   - Director setzt pending_resync via dashboard-api request_resync
 *   - Heartbeat liefert resync:true nur wenn recording_active=false
 *   - pending_resync wird beim Stop/Start zur neuen Session uebernommen
 *   - dashboard-api Endpoints erfordern Auth
 *   - Hard-Resync waehrend Aufnahme wird gating — Flag bleibt gesetzt
 */

declare(strict_types=1);

require_once __DIR__ . '/bootstrap.php';

final class ResyncTest extends MwTestCase
{
    private function seedOnlineSession(string $name, string $cam = 'A', int $pending = 0, int $recActive = 1): int
    {
        $db = get_db();
        $db->prepare(
            "INSERT INTO sessions (user_name, camera_id, status, started_at, last_heartbeat, pending_resync, last_recording_active)
             VALUES (:n, :c, 'online', NOW(), NOW(), :p, :r)"
        )->execute([':n' => $name, ':c' => $cam, ':p' => $pending, ':r' => $recActive]);
        return (int) $db->lastInsertId();
    }

    private function seedOfflineSession(string $name, string $cam = 'A', int $pending = 0): int
    {
        $db = get_db();
        $db->prepare(
            "INSERT INTO sessions (user_name, camera_id, status, started_at, stopped_at, last_heartbeat, pending_resync, last_recording_active)
             VALUES (:n, :c, 'offline', NOW(), NOW(), NOW(), :p, 0)"
        )->execute([':n' => $name, ':c' => $cam, ':p' => $pending]);
        return (int) $db->lastInsertId();
    }

    private function fetchSession(int $id): array
    {
        $stmt = get_db()->prepare("SELECT * FROM sessions WHERE id = :id");
        $stmt->execute([':id' => $id]);
        return $stmt->fetch() ?: [];
    }

    private function callHeartbeat(array $input): JsonResponseException
    {
        try {
            handle_heartbeat($input);
            $this->fail('handle_heartbeat should always exit via json_response');
        } catch (JsonResponseException $e) {
            return $e;
        }
    }

    private function callRequestResync(int $sessionId): JsonResponseException
    {
        $_SERVER['REQUEST_METHOD'] = 'POST';

        /* php://input ueber temp-stream simulieren */
        $stream = fopen('php://memory', 'w+');
        fwrite($stream, json_encode(['session_id' => $sessionId]));
        rewind($stream);

        /* dashboard-api liest file_get_contents('php://input'). Wir koennen
         * php://input nicht direkt simulieren, deshalb monkey-patchen wir
         * indirekt: das produktive handle_request_resync nutzt
         * file_get_contents('php://input'), aber das laesst sich im CLI-PHP
         * nicht einfach setzen. Workaround: temp-Datei via Stream-Wrapper. */
        stream_wrapper_unregister('php');
        stream_wrapper_register('php', MwTestInputStream::class);
        MwTestInputStream::$content = json_encode(['session_id' => $sessionId]);

        try {
            try {
                handle_request_resync();
                $this->fail('handle_request_resync should always exit');
            } catch (JsonResponseException $e) {
                return $e;
            }
        } finally {
            stream_wrapper_restore('php');
        }
    }

    /* ---- Heartbeat-Gating ---- */

    public function test_resync_delivered_when_recording_inactive(): void
    {
        $id = $this->seedOfflineSession('Frank', 'A', /* pending */ 1);

        $e = $this->callHeartbeat([
            'name' => 'Frank',
            'recording_active' => false,
        ]);

        $this->assertSame(200, $e->http_status);
        $this->assertTrue($e->data['resync']);

        /* Flag muss nach Auslieferung geloescht sein */
        $row = $this->fetchSession($id);
        $this->assertSame(0, (int) $row['pending_resync']);
    }

    public function test_resync_NOT_delivered_when_recording_active(): void
    {
        /* Hard-Resync waehrend Aufnahme wuerde den Timecode zerstoeren
         * (siehe TICKET-008 + TICKET-036). Server muss gating durchsetzen. */
        $id = $this->seedOnlineSession('Greta', 'A', /* pending */ 1);

        $e = $this->callHeartbeat([
            'name' => 'Greta',
            'recording_active' => true,
        ]);

        $this->assertSame(200, $e->http_status);
        $this->assertArrayNotHasKey('resync', $e->data,
            'resync MUST NOT be delivered during active recording');

        /* Flag bleibt gesetzt — wird beim naechsten Idle-Heartbeat geliefert */
        $row = $this->fetchSession($id);
        $this->assertSame(1, (int) $row['pending_resync']);
    }

    public function test_no_resync_when_flag_not_set(): void
    {
        $this->seedOfflineSession('Hugo', 'A', /* pending */ 0);

        $e = $this->callHeartbeat([
            'name' => 'Hugo',
            'recording_active' => false,
        ]);

        $this->assertArrayNotHasKey('resync', $e->data);
    }

    /* ---- Pending-Flag-Propagation ueber stop/start ---- */

    public function test_pending_flag_propagated_to_new_session_on_start(): void
    {
        /* Szenario: User idle, Director klickt Resync (setzt Flag auf
         * offline-Session), bevor naechster Idle-Heartbeat das Flag
         * abholt startet der User eine neue Aufnahme. Das Flag muss zur
         * neuen Session uebernommen werden. */
        $old = $this->seedOfflineSession('Ida', 'A', /* pending */ 1);

        /* Neue Aufnahme starten */
        try {
            handle_start([
                'name' => 'Ida',
                'camera_id' => 'A',
            ]);
            $this->fail('handle_start should exit via json_response');
        } catch (JsonResponseException $e) {
            $this->assertSame(200, $e->http_status);
        }

        /* Neue Session muss existieren und das Flag uebernehmen */
        $stmt = get_db()->prepare(
            "SELECT * FROM sessions WHERE user_name='Ida' AND status='online' ORDER BY id DESC LIMIT 1"
        );
        $stmt->execute();
        $newRow = $stmt->fetch();
        $this->assertNotEmpty($newRow);
        $this->assertSame(1, (int) $newRow['pending_resync'],
            'pending_resync from previous session must propagate');

        /* Alte Session sollte offline gemacht worden sein durch start-Handler */
        $oldRow = $this->fetchSession($old);
        $this->assertSame('offline', $oldRow['status']);
    }

    public function test_new_session_without_pending_flag_starts_clean(): void
    {
        /* Wenn die Vor-Session kein Flag hatte, darf die neue auch keins haben */
        $this->seedOfflineSession('Jan', 'A', /* pending */ 0);

        try {
            handle_start(['name' => 'Jan', 'camera_id' => 'A']);
        } catch (JsonResponseException $e) {
            /* expected */
        }

        $stmt = get_db()->prepare(
            "SELECT pending_resync FROM sessions WHERE user_name='Jan' AND status='online'"
        );
        $stmt->execute();
        $this->assertSame(0, (int) $stmt->fetchColumn());
    }

    /* ---- Dashboard-API request_resync ----
     * Der request_resync-Handler selbst prueft KEINE Auth — die zentrale
     * dispatch-Logik in dashboard-api.php ruft require_dashboard_auth()
     * VOR dem Handler-Aufruf. Auth wird daher separat ueber
     * test_dashboard_auth_throws_when_not_authenticated abgedeckt. */

    public function test_dashboard_auth_throws_when_not_authenticated(): void
    {
        $_SESSION = []; /* nicht authentifiziert */
        try {
            require_dashboard_auth();
            $this->fail('require_dashboard_auth should throw when not authenticated');
        } catch (JsonResponseException $e) {
            $this->assertSame(401, $e->http_status);
        }
    }

    public function test_dashboard_auth_passes_when_authenticated(): void
    {
        $_SESSION['mw_authenticated'] = true;
        require_dashboard_auth();
        $this->expectNotToPerformAssertions();
    }

    public function test_request_resync_sets_pending_flag_on_latest_session(): void
    {
        $_SESSION['mw_authenticated'] = true;
        $id = $this->seedOnlineSession('Lara');

        $e = $this->callRequestResync($id);

        $this->assertSame(200, $e->http_status);
        $this->assertTrue($e->data['ok']);

        $row = $this->fetchSession($id);
        $this->assertSame(1, (int) $row['pending_resync']);
    }

    public function test_request_resync_reports_queued_when_recording(): void
    {
        $_SESSION['mw_authenticated'] = true;
        $id = $this->seedOnlineSession('Mira', 'A', 0, /* recActive */ 1);

        $e = $this->callRequestResync($id);
        $this->assertTrue($e->data['queued_during_recording']);
    }

    public function test_request_resync_reports_immediate_when_idle(): void
    {
        $_SESSION['mw_authenticated'] = true;
        /* Online-Session aber recording NOT active = idle aus Plugin-Sicht */
        $id = $this->seedOnlineSession('Nora', 'A', 0, /* recActive */ 0);

        $e = $this->callRequestResync($id);
        $this->assertFalse($e->data['queued_during_recording']);
    }
}

/* Stream-Wrapper, der `php://input` mit Test-Inhalten ersetzt. PHP erlaubt
 * uns nicht den Inhalt von php://input direkt zu setzen, also re-registrieren
 * wir den php-Wrapper temporaer. */
final class MwTestInputStream
{
    public static string $content = '';
    private int $pos = 0;
    public $context;

    public function stream_open($path, $mode, $options, &$opened_path): bool
    {
        return true;
    }

    public function stream_read($count): string
    {
        $chunk = substr(self::$content, $this->pos, $count);
        $this->pos += strlen($chunk);
        return $chunk;
    }

    public function stream_eof(): bool
    {
        return $this->pos >= strlen(self::$content);
    }

    public function stream_stat(): array
    {
        return ['size' => strlen(self::$content)];
    }

    public function url_stat($path, $flags): array
    {
        return ['size' => strlen(self::$content)];
    }
}
