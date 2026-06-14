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
 * ltc-source.h - OBS audio source for LTC timecode output
 *
 * This is the OBS integration layer. It depends on OBS headers.
 */

#ifndef LTC_SOURCE_H
#define LTC_SOURCE_H

#include <obs-module.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sync method tracking — wire-compatible int values for JSON payloads. */
typedef enum {
	SYNC_METHOD_NONE = 0,
	SYNC_METHOD_NTP = 1,
	SYNC_METHOD_HTTP = 2,
	SYNC_METHOD_LOCAL = 3,
} sync_method_t;

/* Register the LTC source with OBS */
void ltc_source_register(void);

/*
 * Full diagnostic snapshot of the first LTC source (TICKET-075).
 *
 * One struct feeds all three remote-debugging channels — MW heartbeat
 * (live dashboard), metadata sidecar (post-mortem), and log lines — so
 * the developer/director see the SAME numbers everywhere:
 *
 *   raw_offset_ms     — latest raw NTP measurement (how far the OS clock
 *                       is off)
 *   applied_offset_ms — what the encoder is currently using
 *   applied_delta_ms  — raw - applied: how far the RECORDED timecode is
 *                       from correct right now (the number the director
 *                       actually cares about during a take; 0 = slewing
 *                       has fully converged)
 *   rtt_ms            — roundtrip of the accepted NTP sample = line
 *                       quality; offset uncertainty is bounded by ±rtt/2
 *   offset_age_sec    — seconds since the measurement, -1 = never synced
 *   sync_method       — sync_method_t as int
 *   synced            — latest sync attempt succeeded
 *   sync_lost_in_session — sticky TICKET-043 flag
 *   initial_skew_ms   — PC clock error found at the FIRST sync
 *                       (TICKET-044); 0 = none detected. Tells the
 *                       developer whose Windows clock was broken at boot.
 *   nominal_fps       — LTC framerate of the source
 */
typedef struct {
	int64_t raw_offset_ms;
	int64_t applied_offset_ms;
	int64_t applied_delta_ms;
	int64_t rtt_ms;
	int offset_age_sec;
	int sync_method;
	bool synced;
	bool sync_lost_in_session;
	int64_t initial_skew_ms;
	int nominal_fps;
} ltc_diag_t;

/*
 * Fill `out` from the first LTC source in this OBS instance.
 * Returns false (and zeroes `out`, age = -1) when no LTC source exists.
 * Lock-free volatile reads; safe to call from any thread.
 */
bool ltc_source_get_diag(ltc_diag_t *out);

/*
 * Read the current NTP offset from the first LTC source in this OBS instance.
 *
 * Outputs (only valid when the call returns true):
 *   offset_ms        — latest raw NTP offset measurement in ms (same as
 *                      raw_offset_ms; kept named offset_ms for back-compat
 *                      with the heartbeat JSON contract).
 *   sync_method      — which sync method last succeeded (cast from sync_method_t)
 *   synced           — true if the latest sync attempt succeeded
 *   raw_offset_ms    — optional; same as offset_ms. Pass NULL if not needed.
 *   offset_age_sec   — optional; whole seconds since the offset was measured,
 *                      or -1 if no successful sync has happened yet. Pass NULL
 *                      if not needed.
 *   sync_lost_in_session — optional; true if at any point during the current
 *                      plugin lifetime an active recording overlapped with a
 *                      sustained NTP-sync failure. Sticky for the session
 *                      (resets only on plugin reload). TICKET-043. Pass NULL
 *                      if not needed.
 *
 * Returns false (and zeroes the outputs) when no LTC source exists.
 *
 * Assumption: at most one LTC source per OBS instance (the Minewache template
 * ships exactly one). If multiple exist, the first found wins.
 *
 * Lock-free read: all backing fields are volatile and atomic-sized on the
 * target platforms; safe to call from any thread.
 */
bool ltc_source_get_current_offset(int64_t *offset_ms, int *sync_method, bool *synced, int64_t *raw_offset_ms,
				   int *offset_age_sec, bool *sync_lost_in_session);

/*
 * Trigger an immediate NTP re-query on every LTC source's sync thread.
 *
 * Used by the MW recording module to honour a director-issued "re-sync"
 * command. Caller must have already verified that recording is NOT active —
 * a hard NTP re-sync during recording causes timecode jumps that break the
 * DaVinci Resolve sync (see TICKET-008 / TICKET-036 in PROJECT.md).
 *
 * Safe to call from any thread. Returns the number of sources kicked.
 */
int ltc_source_kick_resync(void);

/*
 * Signal that an OBS recording just stopped (TICKET-059).
 *
 * Sets `ntp_sync_recovered_edge = true` on every LTC source so the
 * encoder applies the current NTP target offset INSTANTLY on the next
 * non-recording frame, instead of slewing at 10 ms/frame. Fixes the
 * "50 s offset persists across takes" field bug: a PC clock that's
 * wildly wrong (Windows Time service broken, manual misconfiguration)
 * produces an NTP offset that slewing alone cannot close within a
 * normal between-takes window — so the next recording starts with a
 * still-mostly-wrong applied offset.
 *
 * Discontinuity is acceptable here because:
 *   - The jump happens while no recording is being written
 *   - Next recording begins with a fresh, correct applied baseline
 *   - Resolve never sees a TC jump inside any single file
 *
 * Safe to call from any thread. Returns the number of sources signalled.
 */
int ltc_source_signal_recording_stopped(void);

#ifdef __cplusplus
}
#endif

#endif /* LTC_SOURCE_H */
