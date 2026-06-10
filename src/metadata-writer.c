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

/* Escape backslashes in Windows paths for JSON output */
static void write_escaped_path(FILE *f, const char *path)
{
	for (const char *p = path; *p; p++) {
		if (*p == '\\')
			fputc('/', f);
		else
			fputc(*p, f);
	}
}

static void write_sidecar_end(const char *recording_path,
			      struct metadata_writer *mw)
{
	/* Compute sidecar path */
	char sidecar_path[1024];
	snprintf(sidecar_path, sizeof(sidecar_path), "%s", recording_path);
	char *dot = strrchr(sidecar_path, '.');
	if (dot)
		snprintf(dot,
			 sizeof(sidecar_path) - (size_t)(dot - sidecar_path),
			 ".ltc.json");
	else
		return;

	/* Refresh the sync fields from the live LTC source. metadata_writer_
	 * set_info() only runs when the user changes source settings, so
	 * without this refresh the sidecar would report the create-time
	 * values (ntp_synced=false, ntp_offset_ms=0) for every recording —
	 * defeating the purpose of the audit trail. camera_id / framerate /
	 * ntp_server stay from set_info (they don't change during a take). */
	{
		int64_t off = 0;
		int method = 0;
		bool synced = false;
		if (ltc_source_get_current_offset(&off, &method, &synced, NULL,
						  NULL, NULL)) {
			mw->ntp_synced = synced;
			mw->ntp_offset_ms = off;
			const char *m = (method == SYNC_METHOD_NTP) ? "NTP"
				      : (method == SYNC_METHOD_HTTP)
						? "HTTP"
						: "local";
			snprintf(mw->sync_method, sizeof(mw->sync_method), "%s",
				 m);
		}
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

	fprintf(f, "{\n");
	fprintf(f, "  \"plugin\": \"obs-ltc-timecode\",\n");
	fprintf(f, "  \"version\": \"%s\",\n", PLUGIN_VERSION);
	fprintf(f, "  \"camera_id\": \"%c\",\n", 'A' + mw->camera_id);
	fprintf(f, "  \"framerate\": \"%s\",\n", mw->framerate_str);
	fprintf(f, "  \"ntp_server\": \"%s\",\n", mw->ntp_server);
	fprintf(f, "  \"ntp_synced\": %s,\n",
		mw->ntp_synced ? "true" : "false");
	fprintf(f, "  \"ntp_offset_ms\": %lld,\n",
		(long long)mw->ntp_offset_ms);
	fprintf(f, "  \"sync_method\": \"%s\",\n", mw->sync_method);
	fprintf(f, "  \"recording_start\": \"%s\",\n", start_iso);
	fprintf(f, "  \"recording_stop\": \"%s\",\n", end_iso);
	fprintf(f, "  \"duration_seconds\": %.0f,\n", duration);
	fprintf(f, "  \"start_timecode\": \"%s\",\n", mw->start_timecode);
	fprintf(f, "  \"end_timecode\": \"%s\",\n", mw->current_timecode);
	fprintf(f, "  \"recording_file\": \"");
	write_escaped_path(f, recording_path);
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
