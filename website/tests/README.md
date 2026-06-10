# MW Aufnahme Backend Tests (TICKET-038)

PHPUnit-Tests gegen eine echte MariaDB-Test-Datenbank. Verifizieren den
HTTP-Vertrag zwischen Plugin (`mw-recording.c`) und Server (`api.php`,
`dashboard-api.php`).

## Voraussetzungen

- PHP 8.1+ mit `pdo_mysql` und `mbstring`
- Composer (https://getcomposer.org)
- Eine MariaDB/MySQL — entweder lokal oder per Docker (empfohlen)

> **Warum nicht SQLite?** Das Schema nutzt `ENUM`, `ENGINE=InnoDB`,
> `NOW()`, `DATE_SUB(NOW(), INTERVAL ...)`, `UPDATE ... ORDER BY id DESC
> LIMIT 1`, `SET FOREIGN_KEY_CHECKS=0` und `TRUNCATE TABLE` — alles
> MySQL/MariaDB-Dialekt. Production laeuft auf MariaDB; Tests muessen
> denselben Dialekt sprechen, sonst koennten Migrations-/SQL-Bugs in
> Tests gruen sein und in Production rot.

## Setup (empfohlen: Docker)

Im Repo-Root liegt `docker-compose.test.yml` — eine **ephemere** MariaDB
auf Port **3307** (bewusst nicht 3306, damit eine lokale Production-DB
nicht getroffen werden kann). Volume-frei: jeder `up` startet sauber.

```bash
# einmalig: Container starten
docker compose -f docker-compose.test.yml up -d --wait

# Tests laufen lassen
cd website/
composer install   # nur beim ersten Mal
composer test

# wenn fertig:
docker compose -f docker-compose.test.yml down
```

`config-test.php` zeigt per Default auf `127.0.0.1:3307` mit Passwort
`testroot` — passt direkt zum Compose-File.

## Setup (Alternative: lokale MariaDB)

Wenn schon eine MariaDB lokal laeuft:

```bash
cd website/
composer install
MW_TEST_DB_HOST=127.0.0.1 \
MW_TEST_DB_PORT=3306 \
MW_TEST_DB_USER=root \
MW_TEST_DB_PASS=dein_pw \
  composer test
```

## DB-Credentials per Environment

```bash
MW_TEST_DB_HOST=db.local \
MW_TEST_DB_PORT=3307 \
MW_TEST_DB_USER=tester \
MW_TEST_DB_PASS=secret \
MW_TEST_DB_NAME=my_test \
  composer test
```

Die DB wird vor jedem Lauf **gedropt und neu angelegt** — der Bootstrap
weigert sich aus Sicherheitsgruenden gegen DB-Namen die nicht auf `_test`
enden.

## Was wird getestet

**HeartbeatTest**
- Heartbeat akzeptiert offset_ms + sync_method
- Range-Validierung fuer offset_ms (+/- 1 Tag) und sync_method (0-3)
- Idle-Heartbeats updaten die LATESTE Session auch wenn offline
- Mehrere Sessions: nur die neueste wird upgedated
- Empty name → 400
- Legacy-Plugin ohne recording_active → defensives true

**ResyncTest**
- `resync:true` nur bei `recording_active=false` (gating)
- Flag bleibt gesetzt wenn Aufnahme aktiv (queued)
- `pending_resync` propagiert ueber stop/start zur neuen Session
- Saubere Trennung von Sessions ohne Flag
- `require_dashboard_auth()` blockiert ohne Session
- `request_resync` setzt Flag auf neuester Session
- Antwort signalisiert `queued_during_recording` korrekt

## Architektur

Die Tests rufen die `handle_*`-Funktionen direkt auf — keine HTTP-Roundtrips.
Dafuer mussten in der Produktion zwei kleine Anpassungen gemacht werden:

1. `json_response()` (in `includes/db.php`) throwt `JsonResponseException`
   wenn `MW_TEST_MODE` definiert ist, statt zu `exit`. Tests fangen die
   Exception ab und inspizieren `$e->data` / `$e->http_status`.
2. `api.php` und `dashboard-api.php` skippen die Dispatch-Logik wenn
   `MW_TEST_MODE` gesetzt ist — Handler-Funktionen werden trotzdem
   definiert.

Beide Aenderungen sind no-ops im Production-Pfad.
