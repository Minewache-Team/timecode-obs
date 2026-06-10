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
 * ntp-client.h - Minimal SNTP client for NTP time synchronization
 *
 * This module has ZERO OBS dependencies and can be tested standalone.
 */

#ifndef NTP_CLIENT_H
#define NTP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NTP sync result */
typedef struct {
	bool success;
	int64_t offset_ms; /* Offset in milliseconds: positive = local ahead */
	int64_t roundtrip_ms;
} ntp_result_t;

/* NTP client context (opaque) */
typedef struct ntp_client ntp_client_t;

/*
 * Initialize platform networking (Winsock on Windows).
 * Call once at plugin load. Returns true on success.
 */
bool ntp_platform_init(void);

/*
 * Cleanup platform networking.
 * Call once at plugin unload.
 */
void ntp_platform_cleanup(void);

/*
 * Query an NTP server and calculate the clock offset.
 *
 * @param server   NTP server hostname (e.g., "pool.ntp.org")
 * @param timeout_ms  Timeout in milliseconds (recommended: 2000)
 * @param result   Output result
 * @return true on success
 */
bool ntp_query(const char *server, int timeout_ms, ntp_result_t *result);

/*
 * Get the current NTP-corrected Unix time.
 *
 * @param offset_ms  NTP offset to apply (from ntp_query)
 * @param out_sec    Output: seconds since epoch
 * @param out_usec   Output: microseconds fraction
 */
void ntp_corrected_time(int64_t offset_ms, int64_t *out_sec, int64_t *out_usec);

/*
 * Pick the most trustworthy measurement out of `count` samples: the one
 * with the lowest roundtrip time.
 *
 * Rationale: the cameras sit on heterogeneous consumer connections all
 * over Germany. A single SNTP sample's offset error is bounded by
 * ±roundtrip/2 (path asymmetry — DSL/cable uplinks are slower than the
 * downlink, and bufferbloat inflates the RTT by whole seconds while
 * anything else in the household is uploading). Minimising the RTT
 * therefore directly minimises the worst-case offset error, without any
 * assumption about the local clock (which may legitimately step, e.g.
 * when the Windows time service kicks in — history-based outlier
 * rejection would wrongly suppress such a real correction).
 *
 * Samples with success == false are ignored. Samples with
 * roundtrip_ms > max_rtt_ms are ignored (pass max_rtt_ms <= 0 to
 * disable the cap).
 *
 * Returns the index of the best usable sample, or -1 if none qualifies.
 * Pure function; no side effects (testable without network).
 */
int ntp_select_best_sample(const ntp_result_t *samples, int count,
			   int64_t max_rtt_ms);

/*
 * Move `applied_ms` one step toward `target_ms`, clamped to ±max_step_ms.
 * Returns the new applied value. Pure arithmetic; no side effects.
 *
 * Used by the LTC encoder loop to slew the applied NTP offset toward the
 * latest raw measurement without producing TC discontinuities. Keeping this
 * standalone (no OBS / threading deps) lets us unit-test the convergence
 * math directly — mirrors the testable-core pattern used elsewhere.
 *
 * max_step_ms <= 0 is treated as a no-op (returns applied_ms unchanged).
 */
int64_t ntp_slew_step(int64_t applied_ms, int64_t target_ms, int64_t max_step_ms);

#ifdef __cplusplus
}
#endif

#endif /* NTP_CLIENT_H */
