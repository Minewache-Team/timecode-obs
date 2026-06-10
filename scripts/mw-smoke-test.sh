#!/usr/bin/env bash
# MW Smoke Test (TICKET-039)
#
# End-to-end exercise of the plugin↔server heartbeat contract. Requires:
#   - PHP server running (default http://localhost:8000)
#   - MariaDB/MySQL with test schema applied (run `php scripts/mw-smoke-helper.php install` once)
#   - curl, jq
#
# Usage:
#   ./scripts/mw-smoke-test.sh                          # uses defaults
#   BASE_URL=http://example.com API_KEY=xxx ./scripts/mw-smoke-test.sh

set -euo pipefail

BASE_URL="${BASE_URL:-http://localhost:8000}"
API_KEY="${API_KEY:-test_api_key_12345}"
USER_NAME="${USER_NAME:-SmokeTester}"
CAMERA_ID="${CAMERA_ID:-A}"

PASS=0
FAIL=0

c_red()   { printf '\033[31m%s\033[0m' "$1"; }
c_green() { printf '\033[32m%s\033[0m' "$1"; }
c_blue()  { printf '\033[34m%s\033[0m' "$1"; }

step() { echo; c_blue "[STEP] $1"; echo; }

assert_eq() {
    local got="$1" want="$2" label="$3"
    if [ "$got" = "$want" ]; then
        c_green "  ✓"; echo " $label = $got"
        PASS=$((PASS + 1))
    else
        c_red "  ✗"; echo " $label: got '$got' expected '$want'"
        FAIL=$((FAIL + 1))
    fi
}

assert_true() {
    local got="$1" label="$2"
    if [ "$got" = "true" ]; then
        c_green "  ✓"; echo " $label"
        PASS=$((PASS + 1))
    else
        c_red "  ✗"; echo " $label: expected true, got '$got'"
        FAIL=$((FAIL + 1))
    fi
}

assert_missing() {
    local got="$1" label="$2"
    if [ "$got" = "null" ] || [ -z "$got" ]; then
        c_green "  ✓"; echo " $label (not present)"
        PASS=$((PASS + 1))
    else
        c_red "  ✗"; echo " $label: expected missing, got '$got'"
        FAIL=$((FAIL + 1))
    fi
}

api_post() {
    local action="$1" body="$2"
    curl -sS -X POST \
        -H "Content-Type: application/json" \
        -H "X-API-Key: ${API_KEY}" \
        -d "${body}" \
        "${BASE_URL}/api.php?action=${action}"
}

helper() {
    php "$(dirname "$0")/mw-smoke-helper.php" "$@"
}

# ---- Begin ----

echo "MW Smoke Test"
echo "  BASE_URL = ${BASE_URL}"
echo "  API_KEY  = ${API_KEY}"
echo "  USER     = ${USER_NAME}, camera ${CAMERA_ID}"

step "0. Reset DB"
helper reset > /dev/null

step "1. POST ?action=start  (camera ${CAMERA_ID})"
resp=$(api_post start "{\"name\":\"${USER_NAME}\",\"camera_id\":\"${CAMERA_ID}\"}")
echo "  response: $resp"
assert_true "$(echo "$resp" | jq -r '.ok')" "start returned ok"
session_id=$(echo "$resp" | jq -r '.session_id')
test -n "$session_id" && test "$session_id" != "null" || { c_red "  ✗"; echo " missing session_id"; FAIL=$((FAIL+1)); }

step "2. POST ?action=heartbeat  (recording_active=true, offset=42ms)"
resp=$(api_post heartbeat "{\"name\":\"${USER_NAME}\",\"recording_active\":true,\"offset_ms\":42,\"sync_method\":1}")
echo "  response: $resp"
assert_true "$(echo "$resp" | jq -r '.ok')" "heartbeat returned ok"
assert_eq "$(echo "$resp" | jq -r '.updated')" "1" "1 row updated"
assert_missing "$(echo "$resp" | jq -r '.resync // null')" "resync flag"

step "3. Verify DB now has offset_ms = 42"
db_offset=$(helper get_session "${USER_NAME}" | jq -r '.offset_ms')
assert_eq "$db_offset" "42" "DB offset_ms"

step "4. Heartbeat with recording_active=false (camera idle), no flag set"
resp=$(api_post heartbeat "{\"name\":\"${USER_NAME}\",\"recording_active\":false,\"offset_ms\":15,\"sync_method\":1}")
echo "  response: $resp"
assert_missing "$(echo "$resp" | jq -r '.resync // null')" "resync flag (none queued)"

step "5. Director clicks Resync — set pending_resync=1 directly via helper"
helper set_pending "${USER_NAME}" > /dev/null
db_pending=$(helper get_session "${USER_NAME}" | jq -r '.pending_resync')
assert_eq "$db_pending" "1" "DB pending_resync after set"

step "6. Heartbeat with recording_active=true — gating: NO resync delivery"
resp=$(api_post heartbeat "{\"name\":\"${USER_NAME}\",\"recording_active\":true,\"offset_ms\":50,\"sync_method\":1}")
echo "  response: $resp"
assert_missing "$(echo "$resp" | jq -r '.resync // null')" "resync flag (gated by recording)"
db_pending=$(helper get_session "${USER_NAME}" | jq -r '.pending_resync')
assert_eq "$db_pending" "1" "DB pending_resync STILL set after gated heartbeat"

step "7. Heartbeat with recording_active=false — resync delivered, flag cleared"
resp=$(api_post heartbeat "{\"name\":\"${USER_NAME}\",\"recording_active\":false,\"offset_ms\":20,\"sync_method\":1}")
echo "  response: $resp"
assert_true "$(echo "$resp" | jq -r '.resync // false')" "resync flag delivered"
db_pending=$(helper get_session "${USER_NAME}" | jq -r '.pending_resync')
assert_eq "$db_pending" "0" "DB pending_resync cleared after delivery"

step "8. Heartbeat again (idle) — flag stays cleared"
resp=$(api_post heartbeat "{\"name\":\"${USER_NAME}\",\"recording_active\":false,\"offset_ms\":18,\"sync_method\":1}")
assert_missing "$(echo "$resp" | jq -r '.resync // null')" "resync flag (already consumed)"

step "9. POST ?action=stop"
resp=$(api_post stop "{\"name\":\"${USER_NAME}\",\"camera_id\":\"${CAMERA_ID}\"}")
echo "  response: $resp"
assert_true "$(echo "$resp" | jq -r '.ok')" "stop returned ok"

step "10. pending_resync set during idle, then new recording starts → flag must propagate"
helper set_pending "${USER_NAME}" > /dev/null
resp=$(api_post start "{\"name\":\"${USER_NAME}\",\"camera_id\":\"${CAMERA_ID}\"}")
echo "  start response: $resp"
db_pending=$(helper get_session "${USER_NAME}" | jq -r '.pending_resync')
assert_eq "$db_pending" "1" "DB pending_resync propagated to new session"

# ---- Summary ----

echo
echo "=========================================="
if [ "$FAIL" -eq 0 ]; then
    c_green "PASS"; echo " — $PASS assertions"
    exit 0
else
    c_red "FAIL"; echo " — $PASS passed, $FAIL failed"
    exit 1
fi
