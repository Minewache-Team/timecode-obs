<?php
/**
 * Tests fuer handle_heartbeat() (TICKET-038).
 *
 * Vertrag mit dem Plugin (mw-recording.c):
 *   - akzeptiert {"name","recording_active"} Minimum
 *   - akzeptiert optional offset_ms, sync_method, synced
 *   - validiert offset_ms-Range (+/- 1 Tag)
 *   - validiert sync_method-Range (0-3)
 *   - updated die LATESTE Session des Users, auch wenn offline (Idle-Heartbeat)
 */

declare(strict_types=1);

require_once __DIR__ . '/bootstrap.php';

final class HeartbeatTest extends MwTestCase
{
    /** Hilfsmethode: erzeugt eine online-Session fuer den Test-User */
    private function seedOnlineSession(string $name, string $cam = 'A'): int
    {
        $db = get_db();
        $db->prepare(
            "INSERT INTO sessions (user_name, camera_id, status, started_at, last_heartbeat, last_recording_active)
             VALUES (:n, :c, 'online', NOW(), NOW(), 1)"
        )->execute([':n' => $name, ':c' => $cam]);
        return (int) $db->lastInsertId();
    }

    private function seedOfflineSession(string $name, string $cam = 'A'): int
    {
        $db = get_db();
        $db->prepare(
            "INSERT INTO sessions (user_name, camera_id, status, started_at, stopped_at, last_heartbeat, last_recording_active)
             VALUES (:n, :c, 'offline', NOW(), NOW(), NOW(), 0)"
        )->execute([':n' => $name, ':c' => $cam]);
        return (int) $db->lastInsertId();
    }

    private function fetchSession(int $id): array
    {
        $stmt = get_db()->prepare("SELECT * FROM sessions WHERE id = :id");
        $stmt->execute([':id' => $id]);
        return $stmt->fetch() ?: [];
    }

    /** Erwartet dass handle_heartbeat eine JsonResponseException mit den
     *  spezifizierten Properties wirft. */
    private function callHeartbeat(array $input): JsonResponseException
    {
        try {
            handle_heartbeat($input);
            $this->fail('handle_heartbeat should always exit via json_response');
        } catch (JsonResponseException $e) {
            return $e;
        }
    }

    public function test_accepts_offset_and_sync_method(): void
    {
        $id = $this->seedOnlineSession('Alice');

        $e = $this->callHeartbeat([
            'name' => 'Alice',
            'recording_active' => true,
            'offset_ms' => 42,
            'sync_method' => 1,
        ]);

        $this->assertSame(200, $e->http_status);
        $this->assertTrue($e->data['ok']);
        $this->assertSame(1, $e->data['updated']);
        $this->assertArrayNotHasKey('resync', $e->data);

        $row = $this->fetchSession($id);
        $this->assertSame(42, (int) $row['offset_ms']);
        $this->assertSame(1, (int) $row['sync_method']);
        $this->assertSame(1, (int) $row['last_recording_active']);
    }

    public function test_rejects_offset_outside_plausible_range(): void
    {
        $id = $this->seedOnlineSession('Bob');

        $this->callHeartbeat([
            'name' => 'Bob',
            'recording_active' => true,
            'offset_ms' => 999_999_999, /* > 1 Tag */
            'sync_method' => 1,
        ]);

        /* Implausibler Wert wird auf NULL gemappt (silently dropped),
         * Heartbeat selbst erfolgreich */
        $row = $this->fetchSession($id);
        $this->assertNull($row['offset_ms']);
    }

    public function test_rejects_sync_method_out_of_range(): void
    {
        $id = $this->seedOnlineSession('Charlie');

        $this->callHeartbeat([
            'name' => 'Charlie',
            'recording_active' => true,
            'offset_ms' => 100,
            'sync_method' => 9,
        ]);

        $row = $this->fetchSession($id);
        $this->assertNull($row['sync_method']);
        $this->assertSame(100, (int) $row['offset_ms']); /* offset bleibt valide */
    }

    public function test_idle_heartbeat_updates_offline_session(): void
    {
        /* Nach Aufnahmestopp ist die letzte Session offline. Idle-Heartbeats
         * sollen trotzdem den letzten Stand updaten, damit das Dashboard
         * den Offset auch zwischen Aufnahmen sieht und das Resync-Kommando
         * geliefert werden kann (TICKET-036). */
        $id = $this->seedOfflineSession('Dora');

        $e = $this->callHeartbeat([
            'name' => 'Dora',
            'recording_active' => false,
            'offset_ms' => 250,
            'sync_method' => 2,
        ]);

        $this->assertSame(200, $e->http_status);
        $this->assertSame(1, $e->data['updated']);

        $row = $this->fetchSession($id);
        $this->assertSame(250, (int) $row['offset_ms']);
        $this->assertSame(2, (int) $row['sync_method']);
        $this->assertSame(0, (int) $row['last_recording_active']);
    }

    public function test_heartbeat_updates_only_latest_session_per_user(): void
    {
        /* Wenn der User mehrere Sessions hat, darf nur die neueste das
         * neue last_heartbeat / offset bekommen. */
        $old = $this->seedOfflineSession('Eve');
        sleep(1); /* sicherstellen dass NOW() unterscheidbar ist */
        $new = $this->seedOnlineSession('Eve');

        $this->callHeartbeat([
            'name' => 'Eve',
            'recording_active' => true,
            'offset_ms' => 500,
            'sync_method' => 1,
        ]);

        $oldRow = $this->fetchSession($old);
        $newRow = $this->fetchSession($new);

        $this->assertNull($oldRow['offset_ms'], 'old session must not be touched');
        $this->assertSame(500, (int) $newRow['offset_ms'], 'newest session updated');
    }

    public function test_rejects_empty_name(): void
    {
        $e = $this->callHeartbeat([
            'recording_active' => true,
        ]);
        $this->assertSame(400, $e->http_status);
        $this->assertArrayHasKey('error', $e->data);
    }

    public function test_old_plugin_without_recording_active_defaults_to_true(): void
    {
        /* Defensiv: wenn alte Plugins kein recording_active mitschicken,
         * gehen wir vom Worst-Case (true) aus — kein Resync wird geliefert.
         * Verifiziert dass last_recording_active=1 gespeichert wird. */
        $id = $this->seedOnlineSession('LegacyUser');

        $this->callHeartbeat([
            'name' => 'LegacyUser',
            /* recording_active fehlt absichtlich */
        ]);

        $row = $this->fetchSession($id);
        $this->assertSame(1, (int) $row['last_recording_active']);
    }

    /* ===== TICKET-047: plugin_version ===== */

    public function test_accepts_plugin_version(): void
    {
        $id = $this->seedOnlineSession('VersionUser');

        $this->callHeartbeat([
            'name' => 'VersionUser',
            'recording_active' => true,
            'plugin_version' => '0.6.0',
        ]);

        $row = $this->fetchSession($id);
        $this->assertSame('0.6.0', (string) $row['plugin_version']);
    }

    public function test_accepts_plugin_version_with_prerelease(): void
    {
        $id = $this->seedOnlineSession('PreUser');

        $this->callHeartbeat([
            'name' => 'PreUser',
            'recording_active' => true,
            'plugin_version' => '1.0.0-beta.2',
        ]);

        $row = $this->fetchSession($id);
        $this->assertSame('1.0.0-beta.2', (string) $row['plugin_version']);
    }

    public function test_rejects_malformed_plugin_version_silently(): void
    {
        /* HTML-Injection / SQL-Injection / Newlines: alles MUSS still
         * verworfen werden, sodass der existierende plugin_version-Wert
         * erhalten bleibt (dank COALESCE in der UPDATE-Klausel). */
        $id = $this->seedOnlineSession('SafeUser');

        /* Erst gueltige Version setzen */
        $this->callHeartbeat([
            'name' => 'SafeUser',
            'recording_active' => true,
            'plugin_version' => '0.5.1',
        ]);

        /* Dann Quatsch reinwerfen — der gespeicherte Wert darf sich
         * nicht aendern (zurueck auf NULL ist genauso falsch wie
         * ueberschreiben). */
        $this->callHeartbeat([
            'name' => 'SafeUser',
            'recording_active' => true,
            'plugin_version' => '<script>alert(1)</script>',
        ]);

        $row = $this->fetchSession($id);
        $this->assertSame('0.5.1', (string) $row['plugin_version']);
    }

    public function test_rejects_overlong_plugin_version(): void
    {
        $id = $this->seedOnlineSession('LongUser');

        /* Schon eine bekannte Version vorhanden */
        $this->callHeartbeat([
            'name' => 'LongUser',
            'recording_active' => true,
            'plugin_version' => '0.5.1',
        ]);

        /* > 20 Zeichen wird verworfen */
        $this->callHeartbeat([
            'name' => 'LongUser',
            'recording_active' => true,
            'plugin_version' => '99.99.99-very-long-suffix',
        ]);

        $row = $this->fetchSession($id);
        $this->assertSame('0.5.1', (string) $row['plugin_version']);
    }

    public function test_missing_plugin_version_preserves_existing(): void
    {
        /* Wenn alte Plugins kein Feld senden, soll der zuletzt bekannte
         * Wert nicht ueberschrieben werden (COALESCE schuetzt das). */
        $id = $this->seedOnlineSession('MixedUser');

        $this->callHeartbeat([
            'name' => 'MixedUser',
            'recording_active' => true,
            'plugin_version' => '0.6.0',
        ]);

        /* Naechster Heartbeat ohne plugin_version (z.B. alte Version
         * durch einen Bug, oder kurzer Plugin-Glitch). */
        $this->callHeartbeat([
            'name' => 'MixedUser',
            'recording_active' => true,
        ]);

        $row = $this->fetchSession($id);
        $this->assertSame('0.6.0', (string) $row['plugin_version']);
    }

    /* ===== TICKET-043: sync_lost_in_session ===== */

    public function test_sync_lost_stored_when_plugin_reports_true(): void
    {
        $id = $this->seedOnlineSession('LossUser');

        $this->callHeartbeat([
            'name' => 'LossUser',
            'recording_active' => true,
            'sync_lost_in_session' => true,
        ]);

        $row = $this->fetchSession($id);
        $this->assertSame(1, (int) $row['sync_lost_in_session']);
    }

    public function test_sync_lost_is_sticky_across_heartbeats(): void
    {
        /* Einmal true gemeldet, muss in der DB true bleiben — auch wenn ein
         * spaeterer Heartbeat false sendet (z.B. Plugin-Bug). Sticky-Semantik
         * via GREATEST(). */
        $id = $this->seedOnlineSession('StickyUser');

        $this->callHeartbeat([
            'name' => 'StickyUser',
            'recording_active' => true,
            'sync_lost_in_session' => true,
        ]);

        $this->callHeartbeat([
            'name' => 'StickyUser',
            'recording_active' => true,
            'sync_lost_in_session' => false,
        ]);

        $row = $this->fetchSession($id);
        $this->assertSame(1, (int) $row['sync_lost_in_session']);
    }

    public function test_sync_lost_missing_field_does_not_clear_existing(): void
    {
        /* Wenn das Feld fehlt (alte Plugins) darf der gespeicherte Wert
         * nicht ueberschrieben werden. COALESCE(:sl,0) macht das. */
        $id = $this->seedOnlineSession('LegacyLossUser');

        $this->callHeartbeat([
            'name' => 'LegacyLossUser',
            'recording_active' => true,
            'sync_lost_in_session' => true,
        ]);

        $this->callHeartbeat([
            'name' => 'LegacyLossUser',
            'recording_active' => true,
        ]);

        $row = $this->fetchSession($id);
        $this->assertSame(1, (int) $row['sync_lost_in_session']);
    }
}
