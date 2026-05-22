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
 * mw-recording-helpers.c - Pure helpers extracted for unit testing.
 */

#include "mw-recording-helpers.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

int mw_build_heartbeat_body(char *buf, size_t bufsz,
			    const char *name,
			    bool recording_active,
			    bool have_offset,
			    int64_t offset_ms,
			    int sync_method,
			    bool synced,
			    int64_t raw_offset_ms,
			    int offset_age_sec,
			    const char *plugin_version)
{
	if (!buf || bufsz == 0)
		return -1;
	if (!name)
		name = "";

	bool have_version = (plugin_version != NULL && plugin_version[0] != '\0');

	int n;
	if (have_offset && have_version) {
		n = snprintf(buf, bufsz,
			     "{\"name\":\"%s\","
			     "\"recording_active\":%s,"
			     "\"offset_ms\":%lld,"
			     "\"sync_method\":%d,"
			     "\"synced\":%s,"
			     "\"raw_offset_ms\":%lld,"
			     "\"offset_age_sec\":%d,"
			     "\"plugin_version\":\"%s\"}",
			     name,
			     recording_active ? "true" : "false",
			     (long long)offset_ms,
			     sync_method,
			     synced ? "true" : "false",
			     (long long)raw_offset_ms,
			     offset_age_sec,
			     plugin_version);
	} else if (have_offset) {
		n = snprintf(buf, bufsz,
			     "{\"name\":\"%s\","
			     "\"recording_active\":%s,"
			     "\"offset_ms\":%lld,"
			     "\"sync_method\":%d,"
			     "\"synced\":%s,"
			     "\"raw_offset_ms\":%lld,"
			     "\"offset_age_sec\":%d}",
			     name,
			     recording_active ? "true" : "false",
			     (long long)offset_ms,
			     sync_method,
			     synced ? "true" : "false",
			     (long long)raw_offset_ms,
			     offset_age_sec);
	} else if (have_version) {
		n = snprintf(buf, bufsz,
			     "{\"name\":\"%s\","
			     "\"recording_active\":%s,"
			     "\"plugin_version\":\"%s\"}",
			     name,
			     recording_active ? "true" : "false",
			     plugin_version);
	} else {
		n = snprintf(buf, bufsz,
			     "{\"name\":\"%s\","
			     "\"recording_active\":%s}",
			     name,
			     recording_active ? "true" : "false");
	}

	if (n < 0)
		return -1;
	/* snprintf returns the length it WOULD have written — truncation if >= bufsz */
	if ((size_t)n >= bufsz)
		return -1;
	return n;
}

bool mw_response_has_resync(const char *response_body)
{
	if (!response_body)
		return false;

	/*
	 * Scan for the key "resync" followed by ':', any number of spaces/tabs,
	 * then the literal `true`. We tolerate whitespace because PHP's
	 * json_encode can be reconfigured to emit pretty output — and we'd
	 * rather not break the plugin if someone flips JSON_PRETTY_PRINT on
	 * the server.
	 */
	const char *p = response_body;
	while ((p = strstr(p, "\"resync\"")) != NULL) {
		const char *q = p + strlen("\"resync\"");
		/* Skip whitespace before colon (rare but possible) */
		while (*q == ' ' || *q == '\t')
			q++;
		if (*q != ':') {
			p = q;
			continue;
		}
		q++;
		/* Skip whitespace after colon */
		while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
			q++;
		if (strncmp(q, "true", 4) == 0) {
			/* Make sure it's not "trueish" or similar — next char must
			 * be a JSON terminator: , } space or end of string. */
			char next = q[4];
			if (next == ',' || next == '}' || next == ' ' ||
			    next == '\t' || next == '\n' || next == '\r' ||
			    next == 0)
				return true;
		}
		p = q;
	}
	return false;
}
