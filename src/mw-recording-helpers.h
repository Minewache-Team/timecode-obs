/*
 * obs-ltc-timecode - NTP-synced LTC timecode audio source for OBS Studio
 * Copyright (C) 2024-2026 Ferdmusic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>
 *
 * mw-recording-helpers.h - Pure helpers for the MW recording module
 *
 * These functions are intentionally free of OBS / libcurl / threading
 * dependencies so they can be unit-tested without an OBS runtime.
 * Used by mw-recording.c and tests/test-mw-helpers.cpp.
 */

#ifndef MW_RECORDING_HELPERS_H
#define MW_RECORDING_HELPERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * JSON-escape `src` into `dst`: `"` and `\` get backslash-escaped,
 * \n / \r / \t use their short forms, all other control characters
 * (< 0x20) become \u00XX. UTF-8 multibyte sequences pass through
 * unchanged (JSON is UTF-8 native).
 *
 * Why this exists: user-controlled strings (the display name from the MW
 * settings dialog, file paths in the metadata sidecar) end up inside
 * hand-built JSON. One unescaped quote produces invalid JSON — the server
 * rejects the request with 400 BEFORE any server-side sanitization runs,
 * so the camera silently vanishes from the dashboard with no hint to the
 * operator.
 *
 * Returns the number of characters written (excluding the terminator), or
 * -1 if dst is too small (dst is still null-terminated then). NULL src is
 * treated as an empty string.
 */
int mw_json_escape_string(char *dst, size_t dstsz, const char *src);

/*
 * Build the heartbeat JSON body into `buf`.
 *
 * The output shape MUST stay in sync with what the PHP API expects in
 * website/api.php handle_heartbeat() — this is the contract.
 *
 *   {"name":"<n>","recording_active":<bool>[,"plugin_version":"<v>"],"sync_lost_in_session":<bool>}
 *   {"name":"<n>","recording_active":<bool>,"offset_ms":<i64>,"sync_method":<int>,"synced":<bool>,"raw_offset_ms":<i64>,"offset_age_sec":<int>[,"plugin_version":"<v>"],"sync_lost_in_session":<bool>}
 *
 * Notes:
 *   - `name` is JSON-escaped internally (see mw_json_escape_string) — a
 *     display name containing a quote must not invalidate the whole body.
 *     Server-side PHP `sanitize_name` additionally strips disallowed
 *     characters, but that only runs AFTER json_decode succeeds.
 *   - When `have_offset == false`, the offset fields are omitted entirely
 *     (old plugins talking to new servers get sensible defaults).
 *   - `raw_offset_ms` mirrors `offset_ms` today (both report the latest raw
 *     measurement); the field is kept distinct so a future dashboard can
 *     visualise the slewed/applied value separately without another wire
 *     change.
 *   - `offset_age_sec` is whole seconds since the last successful sync, or
 *     -1 if no sync has happened yet. Helpful for the dashboard to render
 *     "vor 23 s" / staleness colour-coding.
 *   - `plugin_version` (TICKET-047) is the build version string from
 *     `PLUGIN_VERSION` (`plugin-support.h`). NULL or empty omits the field
 *     entirely. Server validates `^[\w.+-]{1,20}$`; if you pass anything
 *     containing a double quote it will be dropped server-side. We don't
 *     escape here because the only producer in-tree is `PLUGIN_VERSION`,
 *     which can only contain `[0-9a-zA-Z.+-]` by CMake's project() rules.
 *   - `sync_lost_in_session` (TICKET-043) is always emitted (never omitted)
 *     so the dashboard can clear a previously shown red marker. true means
 *     the plugin observed `recording_active && consecutive_sync_failures
 *     >= 3` at least once since the source was created.
 *   - `rtt_ms`, `applied_delta_ms`, `initial_skew_ms` (TICKET-075) ride
 *     inside the have_offset block: roundtrip of the accepted NTP sample
 *     (line quality, offset uncertainty ≤ ±rtt/2), live distance between
 *     the recorded timecode and the measured target (0 = converged), and
 *     the PC clock error found at the first sync (0 = none). Disclosed in
 *     datenschutz.php; transmitting them requires consent version >= 3.
 *
 * Returns the number of characters written (excluding the null terminator),
 * or -1 on truncation/error. Buf is always null-terminated when bufsz > 0.
 */
int mw_build_heartbeat_body(char *buf, size_t bufsz,
			    const char *name,
			    bool recording_active,
			    bool have_offset,
			    int64_t offset_ms,
			    int sync_method,
			    bool synced,
			    int64_t raw_offset_ms,
			    int offset_age_sec,
			    const char *plugin_version,
			    bool sync_lost_in_session,
			    int64_t rtt_ms,
			    int64_t applied_delta_ms,
			    int64_t initial_skew_ms);

/*
 * Detect a director-issued re-sync command in the API response.
 *
 * Looks for the substring "resync":true with optional whitespace between
 * the colon and the value, so both of these match:
 *
 *   {"ok":true,"resync":true}
 *   {"ok": true, "resync": true}
 *
 * "resync":false and absence of the key both return false.
 *
 * Substring matching is acceptable because the response shape is owned by
 * code in this repo (website/api.php). If we ever switch to a more complex
 * response, swap this for a tiny JSON parser.
 *
 * Returns false on NULL input.
 */
bool mw_response_has_resync(const char *response_body);

#ifdef __cplusplus
}
#endif

#endif /* MW_RECORDING_HELPERS_H */
