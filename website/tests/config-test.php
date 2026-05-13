<?php
/**
 * Test-Konfiguration fuer PHPUnit (TICKET-038).
 *
 * Wird ueber tests/bootstrap.php geladen — definiert dieselben Konstanten wie
 * includes/config.php, aber gegen eine separate Test-Datenbank.
 *
 * Diese Werte sind absichtlich NICHT gitignored: sie sind reine Test-Defaults,
 * keine Production-Credentials.
 *
 * Override per Environment-Variablen:
 *   MW_TEST_DB_HOST, MW_TEST_DB_USER, MW_TEST_DB_PASS, MW_TEST_DB_NAME
 */

define('DB_HOST',    getenv('MW_TEST_DB_HOST') ?: '127.0.0.1');
define('DB_PORT',    (int)(getenv('MW_TEST_DB_PORT') ?: 3307));
define('DB_USER',    getenv('MW_TEST_DB_USER') ?: 'root');
define('DB_PASS',    getenv('MW_TEST_DB_PASS') ?: 'testroot');
define('DB_NAME',    getenv('MW_TEST_DB_NAME') ?: 'mw_aufnahme_test');
define('DB_CHARSET', 'utf8mb4');

define('API_KEY',             'test_api_key_12345');
define('DASHBOARD_PASSWORD',  'test_pw');
define('HEARTBEAT_TIMEOUT',   90);
define('SSE_INTERVAL',        2);
define('MAX_NAME_LENGTH',     100);
