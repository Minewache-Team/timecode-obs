/*
 * timecode.c - SMPTE Timecode generation from wall-clock time
 */

#include "timecode.h"
#include <stdio.h>
#include <string.h>

bool timecode_from_unix(int64_t unix_sec, int64_t unix_usec, tc_framerate_t fps, smpte_timecode_t *out)
{
	if (!out)
		return false;

	memset(out, 0, sizeof(*out));
	out->fps = fps;
	out->drop_frame = (fps == TC_FPS_29_97_DF);

	/* Extract time-of-day from Unix timestamp */
	int64_t day_seconds = unix_sec % 86400;
	if (day_seconds < 0)
		day_seconds += 86400;

	uint8_t h = (uint8_t)(day_seconds / 3600);
	uint8_t m = (uint8_t)((day_seconds % 3600) / 60);
	uint8_t s = (uint8_t)(day_seconds % 60);

	/* Calculate frame number from fractional second */
	int nominal_fps;
	switch (fps) {
	case TC_FPS_24:
		nominal_fps = 24;
		break;
	case TC_FPS_25:
		nominal_fps = 25;
		break;
	case TC_FPS_29_97_DF:
		nominal_fps = 30;
		break;
	case TC_FPS_30:
		nominal_fps = 30;
		break;
	case TC_FPS_50:
		nominal_fps = 50;
		break;
	case TC_FPS_60:
		nominal_fps = 60;
		break;
	default:
		return false;
	}

	uint8_t f = (uint8_t)((unix_usec * nominal_fps) / 1000000);
	if (f >= nominal_fps)
		f = (uint8_t)(nominal_fps - 1);

	out->hours = h;
	out->minutes = m;
	out->seconds = s;
	out->frames = f;

	/* Drop-frame adjustment for 29.97 fps:
	 * Drop frames 0 and 1 at the start of each minute,
	 * EXCEPT every 10th minute. */
	if (fps == TC_FPS_29_97_DF) {
		if (f < 2 && s == 0 && (m % 10) != 0) {
			out->frames = 2;
		}
	}

	return true;
}

char *timecode_to_string(const smpte_timecode_t *tc, char *buf, size_t size)
{
	if (!tc || !buf || size < 12)
		return buf;

	char sep = tc->drop_frame ? ';' : ':';
	snprintf(buf, size, "%02u:%02u:%02u%c%02u", tc->hours, tc->minutes, tc->seconds, sep, tc->frames);

	return buf;
}
