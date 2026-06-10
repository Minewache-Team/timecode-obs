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
 * metadata-writer.c - Recording metadata sidecar file writer
 */

#ifdef ENABLE_FRONTEND_API

#include "metadata-writer.h"
#include "ltc-source.h"
#include "mw-recording-helpers.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

struct metadata_writer {
	int camera_id;
	char framerate_str[32];
	char ntp_server[256];
	bool ntp_synced;
	int64_t ntp_offset_ms;
	char sync_method[32];
	char current_timecode[16];

	/* Recording state */
	char start_timecode[16];
	time_t start_time;
	bool recording_active;
};

/* Write a string as JSON-safe content: backslashes in Windows paths are
 * normalized to '/', then the result is JSON-escaped (quotes, control
 * chars). Without the escaping, a quote in a path or NTP server name
 * produced an invalid sidecar that downstream tooling can't parse. */
static void write_json_string(FILE *f, const char *s)
{
	char norm[1024];
	size_t i = 0;
	for (const char *p = s; *p && i + 1 < sizeof(norm); p++)
		norm[i++] = (*p == '\\') ? '/' : *p;
	norm[i] = '\0';

	char esc[2048];
	if (mw_json_escape_string(esc, sizeof(esc), norm) < 0)
		esc[0] = '\0'; /* never emit a partially escaped value */
	fputs(esc, f);
}

/* Replace the recording's file extension with .ltc.json — looking for the
 * dot only in the basename. A dot in a directory name ("D:\OBS v1.2\rec")
 * must not be mistaken for the extension, or the sidecar lands as
 * "D:\OBS v1.ltc.json" next to the wrong folder. */
static void make_sidecar_path(char *path, size_t pathsz)
{
	char *base = strrchr(path, '/');
	char *base_w = strrchr(path, '\\');
	if (base_w && (!base || base_w > base))
		base = base_w;
	base = base ? base + 1 : path;

	char *dot = strrchr(base, '.');
	if (dot)
		snprintf(dot, pathsz - (size_t)(dot - path), ".ltc.json");
	else
		snprintf(path + strlen(path), pathsz - strlen(path),
			 ".ltc.json");
}

static void write_sidecar_end(const char *recording_path,
			      struct metadata_writer *mw)
{
	/* Compute sidecar path */
	char sidecar_path[1024];
	snprintf(sidecar_path, sizeof(sidecar_path), "%s", recording_path);
	make_sidecar_path(sidecar_path, sizeof(sidecar_path));

	/* Refresh the sync fields from the live LTC source. metadata_writer_
	 * set_info() only runs when the user changes source settings, so
	 * without this refresh the sidecar would report the create-time
	 * values (ntp_synced=false, ntp_offset_ms=0) for every recording —
	 * defeating the purpose of the audit trail. camera_id / framerate /
	 * ntp_server stay from set_info (they don't change during a take). */
	ltc_diag_t diag;
	bool have_diag = ltc_source_get_diag(&diag);
	if (have_diag) {
		mw->ntp_synced = diag.synced;
		mw->ntp_offset_ms = diag.raw_offset_ms;
		const char *m = (diag.sync_method == SYNC_METHOD_NTP) ? "NTP"
			      : (diag.sync_method == SYNC_METHOD_HTTP)
					? "HTTP"
					: "local";
		snprintf(mw->sync_method, sizeof(mw->sync_method), "%s", m);
	}

	time_t now = time(NULL);
	double duration = difftime(now, mw->start_time);

	char end_iso[64];
	struct tm *utc = gmtime(&now);
	if (utc)
		strftime(end_iso, sizeof(end_iso), "%Y-%m-%dT%H:%M:%SZ", utc);
	else
		snprintf(end_iso, sizeof(end_iso), "unknown");

	/* Rewrite the file with complete data */
	FILE *f = fopen(sidecar_path, "w");
	if (!f) {
		obs_log(LOG_WARNING, "Failed to update sidecar: %s",
			sidecar_path);
		return;
	}

	char start_iso[64];
	struct tm *start_utc = gmtime(&mw->start_time);
	if (start_utc)
		strftime(start_iso, sizeof(start_iso), "%Y-%m-%dT%H:%M:%SZ",
			 start_utc);
	else
		snprintf(start_iso, sizeof(start_iso), "unknown");

	int cam = mw->camera_id;
	if (cam < 0 || cam > 15)
		cam = 0;

	fprintf(f, "{\n");
	fprintf(f, "  \"plugin\": \"obs-ltc-timecode\",\n");
	fprintf(f, "  \"version\": \"%s\",\n", PLUGIN_VERSION);
	fprintf(f, "  \"camera_id\": \"%c\",\n", 'A' + cam);
	fprintf(f, "  \"framerate\": \"%s\",\n", mw->framerate_str);
	fprintf(f, "  \"ntp_server\": \"");
	write_json_string(f, mw->ntp_server);
	fprintf(f, "\",\n");
	fprintf(f, "  \"ntp_synced\": %s,\n",
		mw->ntp_synced ? "true" : "false");
	fprintf(f, "  \"ntp_offset_ms\": %lld,\n",
		(long long)mw->ntp_offset_ms);
	fprintf(f, "  \"sync_method\": \"%s\",\n", mw->sync_method);
	if (have_diag) {
		/* TICKET-075: post-mortem diagnostics. The cutter/developer
		 * reading this weeks later sees the line quality, how far the
		 * recorded TC was from target at stop, whether the PC clock
		 * was broken at boot, and whether sync was lost mid-take —
		 * without anyone having to remember the shoot evening. */
		fprintf(f, "  \"ntp_rtt_ms\": %lld,\n",
			(long long)diag.rtt_ms);
		fprintf(f, "  \"applied_offset_ms\": %lld,\n",
			(long long)diag.applied_offset_ms);
		fprintf(f, "  \"applied_delta_ms\": %lld,\n",
			(long long)diag.applied_delta_ms);
		fprintf(f, "  \"offset_age_sec\": %d,\n",
			diag.offset_age_sec);
		fprintf(f, "  \"initial_clock_skew_ms\": %lld,\n",
			(long long)diag.initial_skew_ms);
		fprintf(f, "  \"sync_lost_in_session\": %s,\n",
			diag.sync_lost_in_session ? "true" : "false");
	}
	fprintf(f, "  \"recording_start\": \"%s\",\n", start_iso);
	fprintf(f, "  \"recording_stop\": \"%s\",\n", end_iso);
	fprintf(f, "  \"duration_seconds\": %.0f,\n", duration);
	fprintf(f, "  \"start_timecode\": \"%s\",\n", mw->start_timecode);
	fprintf(f, "  \"end_timecode\": \"%s\",\n", mw->current_timecode);
	fprintf(f, "  \"recording_file\": \"");
	write_json_string(f, recording_path);
	fprintf(f, "\"\n");
	fprintf(f, "}\n");

	fclose(f);
	obs_log(LOG_INFO, "Metadata sidecar completed: %s (%.0fs)",
		sidecar_path, duration);
}

static void on_recording_event(enum obs_frontend_event event, void *data)
{
	struct metadata_writer *mw = (struct metadata_writer *)data;
	if (!mw)
		return;

	if (event == OBS_FRONTEND_EVENT_RECORDING_STARTED) {
		mw->start_time = time(NULL);
		snprintf(mw->start_timecode, sizeof(mw->start_timecode), "%s",
			 mw->current_timecode);
		mw->recording_active = true;
		/* The final recording filename is only available after the
		 * recording stops (obs_frontend_get_last_recording()), so the
		 * complete sidecar — start AND end data — is written in one
		 * shot on STOPPED below. */
	} else if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
		mw->recording_active = false;

		char *path = obs_frontend_get_last_recording();
		if (path && *path) {
			write_sidecar_end(path, mw);
			bfree(path);
		}
	}
}

metadata_writer_t *metadata_writer_create(void)
{
	metadata_writer_t *mw = bzalloc(sizeof(metadata_writer_t));
	snprintf(mw->current_timecode, sizeof(mw->current_timecode),
		 "00:00:00:00");
	snprintf(mw->start_timecode, sizeof(mw->start_timecode),
		 "00:00:00:00");
	obs_frontend_add_event_callback(on_recording_event, mw);
	return mw;
}

void metadata_writer_set_info(metadata_writer_t *mw, int camera_id,
			      const char *framerate_str,
			      const char *ntp_server, bool ntp_synced,
			      int64_t ntp_offset_ms,
			      const char *sync_method_str)
{
	if (!mw)
		return;
	mw->camera_id = camera_id;
	if (framerate_str)
		snprintf(mw->framerate_str, sizeof(mw->framerate_str), "%s",
			 framerate_str);
	if (ntp_server)
		snprintf(mw->ntp_server, sizeof(mw->ntp_server), "%s",
			 ntp_server);
	mw->ntp_synced = ntp_synced;
	mw->ntp_offset_ms = ntp_offset_ms;
	if (sync_method_str)
		snprintf(mw->sync_method, sizeof(mw->sync_method), "%s",
			 sync_method_str);
}

void metadata_writer_set_timecode(metadata_writer_t *mw, const char *tc_str)
{
	if (!mw || !tc_str)
		return;
	snprintf(mw->current_timecode, sizeof(mw->current_timecode), "%s",
		 tc_str);
}

void metadata_writer_destroy(metadata_writer_t *mw)
{
	if (!mw)
		return;
	obs_frontend_remove_event_callback(on_recording_event, mw);
	bfree(mw);
}

#endif /* ENABLE_FRONTEND_API */
