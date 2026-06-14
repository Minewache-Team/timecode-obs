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
 * timecode.c - SMPTE Timecode generation from wall-clock time
 */

#include "timecode.h"
#include <stdio.h>
#include <string.h>

/* Civil date from Unix epoch day count (Howard Hinnant algorithm) */
static void civil_from_days(int64_t day_count, int *y, int *m, int *d)
{
	day_count += 719468;
	int64_t era = (day_count >= 0 ? day_count : day_count - 146096) / 146097;
	int64_t doe = day_count - era * 146097;
	int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	*y = (int)(yoe + era * 400);
	int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int64_t mp = (5 * doy + 2) / 153;
	*d = (int)(doy - (153 * mp + 2) / 5 + 1);
	*m = (int)(mp < 10 ? mp + 3 : mp - 9);
	if (*m <= 2)
		(*y)++;
}

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

	/* Extract date from Unix timestamp */
	int64_t day_count = unix_sec / 86400;
	if (unix_sec < 0 && unix_sec % 86400 != 0)
		day_count--; /* floor division for negative timestamps */

	int cy, cm, cd;
	civil_from_days(day_count, &cy, &cm, &cd);
	out->year = (uint8_t)(cy % 100);
	out->month = (uint8_t)cm;
	out->day = (uint8_t)cd;

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

	/* Drop-frame (29.97): standard SMPTE 12M renumbering. The label
	 * sequence must advance by exactly one per LTC frame — decoders and
	 * NLEs rely on that to lock. Deriving the label from the wall clock
	 * at nominal 30 fps and clamping frames 0/1 to 2 (old behaviour)
	 * emitted the same label up to three times at every non-10th minute
	 * boundary, because nominal numbering runs 0.1% fast within each
	 * minute and the clamp re-aligned it in one step. Instead: count
	 * elapsed 29.97 fps frame periods since midnight, then apply the
	 * drop-frame renumbering (skip labels :00/:01 at each minute except
	 * every 10th). The label deviates from the wall clock by at most
	 * ~2 frames (66 ms) just before each minute mark — that is the
	 * defined drop-frame behaviour, invisible to humans and identical
	 * on every camera (same wall clock in, same label out). */
	if (fps == TC_FPS_29_97_DF) {
		int64_t day_ms = day_seconds * 1000 + unix_usec / 1000;
		/* Elapsed frames at 30000/1001 fps since midnight */
		int64_t total = (day_ms * 30000) / 1001000;
		/* Renumber into label space: per 10-minute block (17982
		 * frames) 18 labels were dropped; within the block, 2 more
		 * for every full minute past the first (1798 frames each). */
		int64_t blocks = total / 17982;
		int64_t rem = total % 17982;
		if (rem < 2)
			rem = 2; /* first two frames of a block belong to the
				  * non-dropping 10th minute */
		total += 18 * blocks + 2 * ((rem - 2) / 1798);
		out->frames = (uint8_t)(total % 30);
		out->seconds = (uint8_t)((total / 30) % 60);
		out->minutes = (uint8_t)((total / 1800) % 60);
		out->hours = (uint8_t)((total / 108000) % 24);
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
