/*
 * http-time-client.h - HTTP Date header fallback for time synchronization
 *
 * When NTP (UDP 123) is blocked, this module provides a fallback by
 * parsing the Date header from an HTTPS HEAD request. Accuracy is
 * ~1 second (vs NTP's ~10-50ms), which is acceptable as a fallback.
 *
 * This module has ZERO OBS dependencies and can be tested standalone.
 */

#ifndef HTTP_TIME_CLIENT_H
#define HTTP_TIME_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	HTTP_TIME_OK,
	HTTP_TIME_CURL_ERROR,
	HTTP_TIME_PARSE_ERROR,
	HTTP_TIME_TIMEOUT,
} http_time_status_t;

typedef struct {
	http_time_status_t status;
	int64_t offset_ms;  /* offset = server_time - local_time (ms) */
	int64_t latency_ms; /* request round-trip time */
} http_time_result_t;

/*
 * Initialize curl globally. Call once at plugin load.
 * Returns true on success.
 */
bool http_time_init(void);

/*
 * Cleanup curl globally. Call once at plugin unload.
 */
void http_time_cleanup(void);

/*
 * Query time via HTTP Date header from an HTTPS endpoint.
 * Uses HEAD request to minimize bandwidth.
 *
 * @param url         HTTPS URL (e.g., "https://www.google.com")
 * @param timeout_ms  Timeout in milliseconds
 * @param result      Output result with offset
 * @return true on success
 */
bool http_time_query(const char *url, int timeout_ms,
		     http_time_result_t *result);

/*
 * Parse an HTTP Date header string into Unix timestamp.
 * Format: "Sun, 01 Mar 2026 12:34:56 GMT" (RFC 7231)
 *
 * @param date_str  The Date header value
 * @param out_sec   Output: seconds since epoch (UTC)
 * @return true on success
 */
bool http_time_parse_date(const char *date_str, int64_t *out_sec);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_TIME_CLIENT_H */
