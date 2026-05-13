<?php
/**
 * PHPUnit Bootstrap (TICKET-038).
 *
 * - Setzt MW_TEST_MODE damit json_response() throwt statt exit()
 * - Laedt config-test.php (statt der echten config.php)
 * - Legt die Test-Datenbank an (DROP & CREATE — Tests starten immer frisch)
 * - Wendet das Schema aus install.php an
 * - Stellt eine TestCase-Basisklasse mit DB-Reset zwischen Tests bereit
 */

declare(strict_types=1);

define('MW_TEST_MODE', true);

require_once __DIR__ . '/config-test.php';

/* DB neu anlegen — wir verbinden zuerst OHNE Datenbankname, dropen+createn,
 * dann erst legt sich `get_db()` per ATTR_PERSISTENT die richtige DB an. */
function mw_test_recreate_database(): void
{
    try {
        $root = new PDO(
            'mysql:host=' . DB_HOST
            . (defined('DB_PORT') ? ';port=' . DB_PORT : '')
            . ';charset=' . DB_CHARSET,
            DB_USER,
            DB_PASS,
            [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]
        );
    } catch (PDOException $e) {
        fwrite(STDERR,
            "\n\nFEHLER: Konnte nicht zu MySQL/MariaDB verbinden ("
            . DB_HOST . "). Setze MW_TEST_DB_* env-Variablen oder pruefe"
            . " dass der DB-Server laeuft.\n\n"
            . $e->getMessage() . "\n");
        exit(1);
    }

    $name = DB_NAME;
    /* Schutz: nur Datenbanken die mit "_test" enden duerfen gedropt werden */
    if (substr($name, -5) !== '_test') {
        fwrite(STDERR,
            "Sicherheitsabbruch: Test-DB '$name' endet nicht auf '_test'.\n");
        exit(1);
    }
    $root->exec("DROP DATABASE IF EXISTS `$name`");
    $root->exec("CREATE DATABASE `$name` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci");
}

mw_test_recreate_database();

/* db.php darf erst NACH der DB-Erstellung geladen werden — die get_db()-
 * Verbindung waere sonst gegen die noch-nicht-existente DB. */
require_once __DIR__ . '/../includes/db.php';

/* Schema anwenden: install.php als Output-buffered include — wir wollen den
 * HTML-Output nicht, nur die Seiteneffekte (CREATE TABLE / ALTER TABLE). */
ob_start();
require __DIR__ . '/../install.php';
ob_end_clean();

/* Basis-Testklasse: TRUNCATE-t Tabellen vor jedem Test fuer Isolation. */
abstract class MwTestCase extends \PHPUnit\Framework\TestCase
{
    protected function setUp(): void
    {
        $db = get_db();
        $db->exec("SET FOREIGN_KEY_CHECKS=0");
        foreach (['sessions', 'scenes', 'consent_log'] as $t) {
            $db->exec("TRUNCATE TABLE `$t`");
        }
        $db->exec("SET FOREIGN_KEY_CHECKS=1");

        /* Authentifizierten Director-Status fuer dashboard-api Tests */
        $_SESSION = [];
    }

    /** Setzt die Eingaben fuer einen API-Aufruf */
    protected function setApiInput(string $action, string $method = 'POST'): void
    {
        $_SERVER['REQUEST_METHOD'] = $method;
        $_GET['action'] = $action;
        $_SERVER['HTTP_X_API_KEY'] = API_KEY;
    }
}

/* Handler-Funktionen werden vor dem ersten Test geladen */
require_once __DIR__ . '/../api.php';
require_once __DIR__ . '/../dashboard-api.php';
