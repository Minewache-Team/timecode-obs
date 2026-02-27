/*
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

#ifdef __cplusplus
}
#endif

#endif /* NTP_CLIENT_H */
