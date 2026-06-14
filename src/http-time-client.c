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
 * http-time-client.c - HTTP Date header fallback for time synchronization
 */

#include "http-time-client.h"
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

static bool curl_initialized = false;

bool http_time_init(void)
{
	if (curl_initialized)
		return true;
	CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
	curl_initialized = (rc == CURLE_OK);
	return curl_initialized;
}

void http_time_cleanup(void)
{
	if (curl_initialized) {
		curl_global_cleanup();
		curl_initialized = false;
	}
}

/* Callback context for extracting Date header */
struct header_data {
	char date_str[128];
	bool found;
};

static size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata)
{
	struct header_data *hd = (struct header_data *)userdata;
	size_t total = size * nitems;

	if (!hd->found && total > 6 && (buffer[0] == 'D' || buffer[0] == 'd') &&
	    (buffer[1] == 'a' || buffer[1] == 'A') && (buffer[2] == 't' || buffer[2] == 'T') &&
	    (buffer[3] == 'e' || buffer[3] == 'E') && buffer[4] == ':' && buffer[5] == ' ') {
		size_t len = total - 6;
		if (len >= sizeof(hd->date_str))
			len = sizeof(hd->date_str) - 1;
		memcpy(hd->date_str, buffer + 6, len);
		/* Strip trailing \r\n */
		while (len > 0 && (hd->date_str[len - 1] == '\r' || hd->date_str[len - 1] == '\n'))
			len--;
		hd->date_str[len] = '\0';
		hd->found = true;
	}
	return total;
}

/* Discard response body */
static size_t discard_body(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	(void)ptr;
	(void)userdata;
	return size * nmemb;
}

/* Get current time in milliseconds since epoch */
static int64_t get_time_ms(void)
{
#ifdef _WIN32
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	ULARGE_INTEGER uli;
	uli.LowPart = ft.dwLowDateTime;
	uli.HighPart = ft.dwHighDateTime;
	/* Windows FILETIME epoch: 1601-01-01, convert to Unix */
	return (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10000);
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

bool http_time_query(const char *url, int timeout_ms, http_time_result_t *result)
{
	if (!url || !result)
		return false;

	memset(result, 0, sizeof(*result));

	if (!curl_initialized) {
		result->status = HTTP_TIME_CURL_ERROR;
		return false;
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		result->status = HTTP_TIME_CURL_ERROR;
		return false;
	}

	struct header_data hd;
	memset(&hd, 0, sizeof(hd));

	int64_t t1 = get_time_ms();

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); /* HEAD request */
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hd);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_body);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)(timeout_ms / 2));
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
	/* Disable SSL verification to avoid cert bundle issues on Windows */
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

	CURLcode res = curl_easy_perform(curl);

	int64_t t4 = get_time_ms();

	curl_easy_cleanup(curl);

	if (res == CURLE_OPERATION_TIMEDOUT) {
		result->status = HTTP_TIME_TIMEOUT;
		return false;
	}

	if (res != CURLE_OK) {
		result->status = HTTP_TIME_CURL_ERROR;
		return false;
	}

	if (!hd.found) {
		result->status = HTTP_TIME_PARSE_ERROR;
		return false;
	}

	int64_t server_sec;
	if (!http_time_parse_date(hd.date_str, &server_sec)) {
		result->status = HTTP_TIME_PARSE_ERROR;
		return false;
	}

	result->latency_ms = t4 - t1;

	/* Offset calculation: server_time - local_time
	 * Use midpoint of request for local time estimate */
	int64_t local_mid_ms = t1 + result->latency_ms / 2;
	int64_t server_ms = server_sec * 1000;
	result->offset_ms = server_ms - local_mid_ms;
	result->status = HTTP_TIME_OK;

	return true;
}

/* Month name lookup for RFC 7231 date parsing */
static int parse_month(const char *mon)
{
	static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
				       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	for (int i = 0; i < 12; i++) {
		if (mon[0] == months[i][0] && mon[1] == months[i][1] && mon[2] == months[i][2])
			return i + 1;
	}
	return 0;
}

/* Days from civil date to Unix epoch (simplified for 1970+) */
static int64_t days_from_civil(int y, int m, int d)
{
	if (m <= 2) {
		y--;
		m += 9;
	} else {
		m -= 3;
	}
	int64_t era = (y >= 0 ? y : y - 399) / 400;
	int64_t yoe = y - era * 400;
	int64_t doy = (153 * m + 2) / 5 + d - 1;
	int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

bool http_time_parse_date(const char *date_str, int64_t *out_sec)
{
	if (!date_str || !out_sec)
		return false;

	/* RFC 7231: "Sun, 01 Mar 2026 12:34:56 GMT"
	 * Skip day name, parse: DD Mon YYYY HH:MM:SS */
	const char *p = date_str;

	/* Skip optional day name + comma + space */
	const char *comma = strchr(p, ',');
	if (comma)
		p = comma + 1;
	while (*p == ' ')
		p++;

	int day, year, hour, min, sec;
	char mon_str[4] = {0};

	/* Parse "DD Mon YYYY HH:MM:SS" */
	if (strlen(p) < 20)
		return false;

	/* DD */
	day = (p[0] - '0') * 10 + (p[1] - '0');
	if (day < 1 || day > 31)
		return false;
	p += 3; /* skip "DD " */

	/* Mon */
	mon_str[0] = p[0];
	mon_str[1] = p[1];
	mon_str[2] = p[2];
	int month = parse_month(mon_str);
	if (month == 0)
		return false;
	p += 4; /* skip "Mon " */

	/* YYYY */
	year = (p[0] - '0') * 1000 + (p[1] - '0') * 100 + (p[2] - '0') * 10 + (p[3] - '0');
	if (year < 1970 || year > 2099)
		return false;
	p += 5; /* skip "YYYY " */

	/* HH:MM:SS */
	hour = (p[0] - '0') * 10 + (p[1] - '0');
	min = (p[3] - '0') * 10 + (p[4] - '0');
	sec = (p[6] - '0') * 10 + (p[7] - '0');

	if (hour > 23 || min > 59 || sec > 60)
		return false;

	int64_t days = days_from_civil(year, month, day);
	*out_sec = days * 86400 + hour * 3600 + min * 60 + sec;

	return true;
}
