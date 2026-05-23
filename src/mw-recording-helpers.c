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
			    const char *plugin_version,
			    bool sync_lost_in_session)
{
	if (!buf || bufsz == 0)
		return -1;
	if (!name)
		name = "";

	bool have_version = (plugin_version != NULL && plugin_version[0] != '\0');
	const char *sync_lost_str = sync_lost_in_session ? "true" : "false";

	/* Build the JSON in two appended halves: required fields first, then
	 * optional fields conditionally. Keeps the four-branch matrix from
	 * earlier collapsed into one path. */
	int n = snprintf(buf, bufsz,
			 "{\"name\":\"%s\",\"recording_active\":%s",
			 name,
			 recording_active ? "true" : "false");
	if (n < 0)
		return -1;
	if ((size_t)n >= bufsz)
		return -1;

	if (have_offset) {
		int m = snprintf(buf + n, bufsz - (size_t)n,
				 ",\"offset_ms\":%lld,\"sync_method\":%d,"
				 "\"synced\":%s,\"raw_offset_ms\":%lld,"
				 "\"offset_age_sec\":%d",
				 (long long)offset_ms,
				 sync_method,
				 synced ? "true" : "false",
				 (long long)raw_offset_ms,
				 offset_age_sec);
		if (m < 0)
			return -1;
		n += m;
		if ((size_t)n >= bufsz)
			return -1;
	}

	if (have_version) {
		int m = snprintf(buf + n, bufsz - (size_t)n,
				 ",\"plugin_version\":\"%s\"",
				 plugin_version);
		if (m < 0)
			return -1;
		n += m;
		if ((size_t)n >= bufsz)
			return -1;
	}

	/* sync_lost_in_session always present when the helper is called from
	 * the plugin (we know the flag); test code that doesn't care can pass
	 * false and the field is rendered as "false" — semantically identical
	 * to "not lost". This deliberately deviates from the
	 * omit-when-not-applicable pattern of the offset/version fields,
	 * because the dashboard needs an explicit signal to clear a previously
	 * shown red marker. */
	int m = snprintf(buf + n, bufsz - (size_t)n,
			 ",\"sync_lost_in_session\":%s",
			 sync_lost_str);
	if (m < 0)
		return -1;
	n += m;
	if ((size_t)n >= bufsz)
		return -1;

	if ((size_t)n + 1 >= bufsz) /* room for closing brace */
		return -1;
	buf[n++] = '}';
	buf[n] = '\0';

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
