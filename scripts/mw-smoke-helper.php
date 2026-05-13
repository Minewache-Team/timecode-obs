<?php
/**
 * MW Smoke Test Helper (TICKET-039).
 *
 * Wird ausschliesslich aus den Smoke-Skripten heraus aufgerufen, um Test-DB-
 * Setup zu erledigen das per HTTP-API nicht moeglich ist (z.B. pending_resync
 * direkt setzen, ohne den Director-Auth-Flow zu durchlaufen).
 *
 * Nur per CLI aufrufbar — keine HTTP-Schnittstelle, damit das nicht
 * versehentlich in Production landet:
 *
 *   php scripts/mw-smoke-helper.php reset
 *   php scripts/mw-smoke-helper.php set_pending <user_name>
 *   php scripts/mw-smoke-helper.php get_session <user_name>
 *
 * Liest dieselbe Konfiguration wie die Tests (website/tests/config-test.php).
 */

declare(strict_types=1);

if (PHP_SAPI !== 'cli') {
    http_response_code(403);
    echo "This helper is CLI-only.\n";
    exit(1);
}

require_once __DIR__ . '/../website/tests/config-test.php';
require_once __DIR__ . '/../website/includes/db.php';

$args = $argv;
array_shift($args); /* script name */
$action = $args[0] ?? '';

function fail(string $msg, int $code = 1): never
{
    fwrite(STDERR, "ERROR: $msg\n");
    exit($code);
}

try {
    $db = get_db();
} catch (PDOException $e) {
    fail('DB connect failed: ' . $e->getMessage());
}

switch ($action) {
    case 'reset':
        /* Tabellen leeren — Tests starten frisch. Schema muss schon angelegt
         * sein (via website/install.php). */
        $db->exec("SET FOREIGN_KEY_CHECKS=0");
        foreach (['sessions', 'scenes', 'consent_log'] as $t) {
            $db->exec("TRUNCATE TABLE `$t`");
        }
        $db->exec("SET FOREIGN_KEY_CHECKS=1");
        echo "ok\n";
        break;

    case 'set_pending':
        $name = $args[1] ?? '';
        if ($name === '') {
            fail('set_pending requires <user_name>');
        }
        $stmt = $db->prepare(
            "UPDATE sessions SET pending_resync = 1
             WHERE user_name = :n AND status != 'removed'
             ORDER BY id DESC LIMIT 1"
        );
        $stmt->execute([':n' => $name]);
        echo $stmt->rowCount() . "\n";
        break;

    case 'get_session':
        $name = $args[1] ?? '';
        if ($name === '') {
            fail('get_session requires <user_name>');
        }
        $stmt = $db->prepare(
            "SELECT id, user_name, camera_id, status, offset_ms, sync_method,
                    pending_resync, last_recording_active, last_heartbeat
             FROM sessions
             WHERE user_name = :n AND status != 'removed'
             ORDER BY id DESC LIMIT 1"
        );
        $stmt->execute([':n' => $name]);
        $row = $stmt->fetch();
        if (!$row) {
            echo "null\n";
            exit(2);
        }
        echo json_encode($row, JSON_PRETTY_PRINT) . "\n";
        break;

    case 'install':
        /* Schema aus install.php anwenden (idempotent) */
        ob_start();
        require __DIR__ . '/../website/install.php';
        ob_end_clean();
        echo "ok\n";
        break;

    default:
        fwrite(STDERR,
            "Usage: php mw-smoke-helper.php <action> [args]\n" .
            "Actions: reset | set_pending <name> | get_session <name> | install\n");
        exit(1);
}
