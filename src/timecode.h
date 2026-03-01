/*
 * timecode.h - SMPTE Timecode generation from wall-clock time
 *
 * This module has ZERO OBS dependencies and can be tested standalone.
 */

#ifndef TIMECODE_H
#define TIMECODE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Supported SMPTE framerates */
typedef enum {
	TC_FPS_24 = 24,
	TC_FPS_25 = 25,
	TC_FPS_29_97_DF = 2997, /* 29.97 drop-frame */
	TC_FPS_30 = 30,
	TC_FPS_50 = 50,
	TC_FPS_60 = 60,
} tc_framerate_t;

/* SMPTE Timecode value */
typedef struct {
	uint8_t hours;
	uint8_t minutes;
	uint8_t seconds;
	uint8_t frames;
	tc_framerate_t fps;
	bool drop_frame;
	/* Date (SMPTE 12M User Bits) */
	uint8_t year;  /* 0-99, two-digit year */
	uint8_t month; /* 1-12 */
	uint8_t day;   /* 1-31 */
} smpte_timecode_t;

/*
 * Convert a Unix timestamp (seconds + microseconds) to SMPTE timecode
 * at the given framerate. Uses time-of-day (UTC).
 *
 * @param unix_sec     Seconds since epoch (UTC)
 * @param unix_usec    Microseconds fraction
 * @param fps          Target framerate
 * @param out          Output timecode struct
 * @return true on success, false on invalid framerate
 */
bool timecode_from_unix(int64_t unix_sec, int64_t unix_usec, tc_framerate_t fps, smpte_timecode_t *out);

/*
 * Format timecode as string "HH:MM:SS:FF" (or "HH:MM:SS;FF" for drop-frame)
 *
 * @param tc   Timecode to format
 * @param buf  Output buffer (must be at least 12 bytes)
 * @param size Buffer size
 * @return pointer to buf
 */
char *timecode_to_string(const smpte_timecode_t *tc, char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* TIMECODE_H */
