# Smoke Tests (TICKET-039)

End-to-end exercise of the plugin↔server heartbeat contract via raw HTTP.
Catches contract drift between `src/mw-recording.c` and `website/api.php`
without needing a live plugin or OBS.

## Files

| File | Purpose |
|------|---------|
| `mw-smoke-test.ps1` | Windows PowerShell runner |
| `mw-smoke-test.sh`  | POSIX bash runner (Linux, macOS, WSL, Git Bash) |
| `mw-smoke-helper.php` | CLI helper for DB seeding (set `pending_resync`, reset, read state) |

The two runners are kept in lockstep — same assertion sequence, same labels.

## Prerequisites

- **PHP CLI** with `pdo_mysql` and `mbstring` extensions.
- **MariaDB or MySQL** — easiest path is the included Docker setup:
  ```sh
  docker compose -f docker-compose.test.yml up -d --wait
  ```
  This brings up an ephemeral MariaDB on `127.0.0.1:3307` (intentionally
  off the default port to avoid clobbering a local production DB).
- **Test schema applied once** (per fresh DB):
  ```sh
  php scripts/mw-smoke-helper.php install
  ```
- **PHP dev server running** for the website:
  ```sh
  php -S localhost:8000 -t website/
  ```
  Note: this requires `website/includes/config.php` to exist and point at
  the test DB. The CI workflow `smoke-test.yaml` shows the exact contents.
- **For the bash version**: `curl` and `jq` on `PATH`.

## Running

```sh
# bash
./scripts/mw-smoke-test.sh

# PowerShell
.\scripts\mw-smoke-test.ps1
```

Both default to `http://localhost:8000` and API key `test_api_key_12345`
(same defaults as `website/tests/config-test.php`).

Override via env or parameters:

```sh
BASE_URL=http://staging.example.com API_KEY=xxx ./scripts/mw-smoke-test.sh
```

```powershell
.\scripts\mw-smoke-test.ps1 -BaseUrl http://staging -ApiKey xxx
```

Exit code is `0` on PASS, `1` on any failure. Suitable for CI.

## What the smoke test exercises

The script runs through the canonical lifecycle plus the safety-critical
gating cases. Each step asserts against both the HTTP response and the
resulting DB state:

| Step | What | Asserts |
|------|------|---------|
| 0 | DB reset | helper completes cleanly |
| 1 | `?action=start` | response.ok, session_id returned |
| 2 | `?action=heartbeat` (recording_active=true, offset=42ms) | ok=true, updated=1, **no resync** |
| 3 | DB state | `offset_ms` persisted |
| 4 | `?action=heartbeat` (recording_active=false, no flag set) | **no resync** |
| 5 | Set `pending_resync=1` via helper | DB flag set |
| 6 | `?action=heartbeat` (recording_active=true, flag set) | **no resync** (gated), flag remains |
| 7 | `?action=heartbeat` (recording_active=false, flag set) | **resync=true**, flag cleared |
| 8 | `?action=heartbeat` (already consumed) | no resync (idempotent) |
| 9 | `?action=stop` | ok=true |
| 10 | Set flag while idle, then `?action=start` | flag propagates to new session |

The gating cases (steps 6 + 7) are the safety-critical ones — they verify
that a director-issued re-sync never lands during an active recording, the
constraint from TICKET-008 / TICKET-036.

## CI

Suitable for a GitHub Actions job: spin up a MariaDB service container,
start `php -S` in the background, run the smoke script. Tracked separately
as part of TICKET-039.
