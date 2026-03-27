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
 * ltc-source.c - OBS audio source for LTC timecode output
 *
 * Integrates NTP sync, SMPTE timecode generation, and LTC audio encoding.
 * Outputs continuous LTC audio via OBS's audio pipeline.
 *
 * Threading model:
 *   - video_tick runs on OBS video thread: reads ntp_offset_ms, uses encoder
 *   - NTP sync thread: writes ntp_offset_ms periodically
 *   - update runs on OBS main thread: may recreate encoder (protected by mutex)
 */

#include "ltc-source.h"
#include "ntp-client.h"
#include "http-time-client.h"
#include "timecode.h"
#include "ltc-encoder-wrapper.h"
#ifdef ENABLE_FRONTEND_API
#include "metadata-writer.h"
#include <obs-frontend-api.h>
#include <util/config-file.h>
#endif

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>
#include <util/threading.h>

#include <string.h>
#include <stdlib.h>
#include <math.h>

#define SAMPLE_RATE 48000
#define AUDIO_BUF_FRAMES 9600 /* 200ms at 48kHz */
#define MAX_FRAME_SAMPLES 4000 /* max samples per LTC frame (48000/24 = 2000) */
#define NTP_QUERY_TIMEOUT_MS 2000
#define NTP_RETRY_COUNT 3
#define EMA_ALPHA 0.3
#define RESYNC_CHECK_FRAMES 750 /* check wall clock drift every ~30s at 25fps */
#define RESYNC_DRIFT_THRESHOLD 2 /* only hard-resync if drift exceeds this many frames */
#define HTTP_FALLBACK_URL "https://www.google.com"
#define HTTP_FALLBACK_TIMEOUT_MS 5000

/* Sync method tracking */
typedef enum {
	SYNC_METHOD_NONE,
	SYNC_METHOD_NTP,
	SYNC_METHOD_HTTP,
	SYNC_METHOD_LOCAL,
} sync_method_t;

/* Settings keys */
#define S_FRAMERATE "framerate"
#define S_NTP_SERVER "ntp_server"
#define S_SYNC_INTERVAL "sync_interval"
#define S_AUDIO_TRACK "audio_track"
#define S_CAMERA_ID "camera_id"

struct ltc_source_context {
	obs_source_t *source;

	/* LTC encoder (protected by encoder_mutex for recreation) */
	ltc_wrapper_t *encoder;
	pthread_mutex_t encoder_mutex;
	tc_framerate_t framerate;
	int nominal_fps;

	/* Per-frame encode buffer */
	float frame_buf[MAX_FRAME_SAMPLES];
	int frame_samples_total;
	int frame_pos;
	bool frame_valid;

	/* Audio output buffer */
	float audio_buf[AUDIO_BUF_FRAMES];
	struct obs_source_audio audio_output;
	uint64_t next_audio_ts;

	/* NTP sync thread */
	pthread_t ntp_thread;
	os_event_t *stop_event;
	bool thread_created;
	char ntp_server[256];
	int sync_interval_sec;

	/* NTP offset (written by NTP thread, read by video_tick) */
	volatile int64_t ntp_offset_ms;
	volatile bool ntp_synced;
	volatile int64_t ntp_roundtrip_ms;
	volatile sync_method_t sync_method;
	bool first_sync_done;

	/* Timecode re-sync tracking */
	uint64_t frames_encoded;

	/* Drift detection: reference point for expected timecode */
	int64_t sync_ref_sec;   /* wall-clock seconds at last hard sync */
	int64_t sync_ref_usec;  /* wall-clock microseconds at last hard sync */
	uint64_t sync_ref_frame; /* frames_encoded at last hard sync */

	/* Audio track routing */
	int audio_track; /* 1-6, default 3 */

	/* Camera identification for LTC User Bits */
	int camera_id; /* 0-7 maps to A-H */

#ifdef ENABLE_FRONTEND_API
	/* Metadata sidecar writer */
	metadata_writer_t *metadata;

#endif
};

/* ---- NTP sync thread ---- */

static void *ntp_sync_thread(void *data)
{
	struct ltc_source_context *ctx = data;

	os_set_thread_name("ltc-ntp-sync");

	while (os_event_timedwait(ctx->stop_event, 0) != 0) {
		ntp_result_t result;
		bool success = false;

		/* Copy server name under lock to avoid data race with update */
		char server_copy[256];
		pthread_mutex_lock(&ctx->encoder_mutex);
		snprintf(server_copy, sizeof(server_copy), "%s",
			 ctx->ntp_server);
		pthread_mutex_unlock(&ctx->encoder_mutex);

		for (int attempt = 0; attempt < NTP_RETRY_COUNT; attempt++) {
			if (os_event_try(ctx->stop_event) == 0)
				return NULL;

			if (ntp_query(server_copy, NTP_QUERY_TIMEOUT_MS,
				      &result)) {
				success = true;
				break;
			}
		}

		if (success) {
			if (!ctx->first_sync_done) {
				ctx->ntp_offset_ms = result.offset_ms;
				ctx->first_sync_done = true;
			} else {
				int64_t old = ctx->ntp_offset_ms;
				int64_t smoothed = (int64_t)(
					EMA_ALPHA * (double)result.offset_ms +
					(1.0 - EMA_ALPHA) * (double)old);
				ctx->ntp_offset_ms = smoothed;
			}
			ctx->ntp_roundtrip_ms = result.roundtrip_ms;
			ctx->ntp_synced = true;
			ctx->sync_method = SYNC_METHOD_NTP;
		} else {
			/* NTP failed — try HTTP Date header fallback */
			http_time_result_t http_result;
			if (http_time_query(HTTP_FALLBACK_URL,
					    HTTP_FALLBACK_TIMEOUT_MS,
					    &http_result)) {
				if (!ctx->first_sync_done) {
					ctx->ntp_offset_ms =
						http_result.offset_ms;
					ctx->first_sync_done = true;
				} else {
					int64_t old = ctx->ntp_offset_ms;
					int64_t smoothed = (int64_t)(
						EMA_ALPHA *
							(double)http_result
								.offset_ms +
						(1.0 - EMA_ALPHA) *
							(double)old);
					ctx->ntp_offset_ms = smoothed;
				}
				ctx->ntp_synced = true;
				ctx->sync_method = SYNC_METHOD_HTTP;
			} else {
				ctx->sync_method = SYNC_METHOD_LOCAL;
			}
		}

		if (os_event_timedwait(ctx->stop_event,
				       (unsigned long)ctx->sync_interval_sec * 1000) == 0)
			return NULL;
	}

	return NULL;
}

static void start_ntp_thread(struct ltc_source_context *ctx)
{
	if (ctx->thread_created)
		return;

	if (os_event_init(&ctx->stop_event, OS_EVENT_TYPE_MANUAL) != 0)
		return;

	if (pthread_create(&ctx->ntp_thread, NULL, ntp_sync_thread, ctx) == 0) {
		ctx->thread_created = true;
	} else {
		os_event_destroy(ctx->stop_event);
		ctx->stop_event = NULL;
	}
}

static void stop_ntp_thread(struct ltc_source_context *ctx)
{
	if (!ctx->thread_created)
		return;

	os_event_signal(ctx->stop_event);
	pthread_join(ctx->ntp_thread, NULL);
	os_event_destroy(ctx->stop_event);
	ctx->stop_event = NULL;
	ctx->thread_created = false;
}

/* ---- Framerate helpers ---- */

static tc_framerate_t detect_obs_framerate(void)
{
	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return TC_FPS_25;

	/*
	 * Check exact integer ratios first — OBS stores fps as num/den.
	 * This avoids ambiguity between 29.97 and 30, or 59.94 and 60.
	 */
	uint32_t num = ovi.fps_num;
	uint32_t den = ovi.fps_den;

	if (den == 1) {
		switch (num) {
		case 24:
			return TC_FPS_24;
		case 25:
			return TC_FPS_25;
		case 30:
			return TC_FPS_30;
		case 50:
			return TC_FPS_50;
		case 60:
			return TC_FPS_60;
		}
	} else if (den == 1001) {
		if (num == 24000)
			return TC_FPS_24;
		if (num == 30000)
			return TC_FPS_29_97_DF;
		if (num == 60000)
			return TC_FPS_60;
	}

	/* Fallback: approximate matching for non-standard ratios.
	 * Check exact rates before their NTSC counterparts to avoid
	 * misdetection (e.g. 30fps must not match 29.97). */
	double fps = (double)num / (double)den;

	if (fabs(fps - 23.976) < 0.3)
		return TC_FPS_24;
	if (fabs(fps - 24.0) < 0.3)
		return TC_FPS_24;
	if (fabs(fps - 25.0) < 0.5)
		return TC_FPS_25;
	if (fabs(fps - 30.0) < 0.01)
		return TC_FPS_30;
	if (fabs(fps - 29.97) < 0.5)
		return TC_FPS_29_97_DF;
	if (fabs(fps - 50.0) < 0.5)
		return TC_FPS_50;
	if (fabs(fps - 60.0) < 0.01)
		return TC_FPS_60;
	if (fabs(fps - 59.94) < 0.5)
		return TC_FPS_60;

	return TC_FPS_25;
}

static bool fps_setting_valid(int setting)
{
	return setting == TC_FPS_24 || setting == TC_FPS_25 ||
	       setting == TC_FPS_29_97_DF || setting == TC_FPS_30 ||
	       setting == TC_FPS_50 || setting == TC_FPS_60;
}

static int fps_nominal(tc_framerate_t fps)
{
	switch (fps) {
	case TC_FPS_24:
		return 24;
	case TC_FPS_25:
		return 25;
	case TC_FPS_29_97_DF:
		return 30;
	case TC_FPS_30:
		return 30;
	case TC_FPS_50:
		return 50;
	case TC_FPS_60:
		return 60;
	default:
		return 25;
	}
}

/* ---- Encoder lifecycle ---- */

static void create_encoder(struct ltc_source_context *ctx)
{
	ctx->encoder = ltc_wrapper_create(SAMPLE_RATE, ctx->framerate);
	ctx->nominal_fps = fps_nominal(ctx->framerate);
	ctx->frame_valid = false;
	ctx->frame_pos = 0;
	ctx->frame_samples_total = 0;
	ctx->frames_encoded = 0;
}

static void recreate_encoder(struct ltc_source_context *ctx, tc_framerate_t new_fps)
{
	pthread_mutex_lock(&ctx->encoder_mutex);
	if (ctx->encoder) {
		ltc_wrapper_destroy(ctx->encoder);
		ctx->encoder = NULL;
	}
	ctx->framerate = new_fps;
	create_encoder(ctx);
	pthread_mutex_unlock(&ctx->encoder_mutex);
}

/* ---- Audio generation ---- */

/*
 * Calculate the total frame number for a timecode at the given fps.
 * Used for drift comparison (not for display).
 */
static int64_t tc_to_total_frames(const smpte_timecode_t *tc, int nominal_fps)
{
	return (int64_t)tc->hours * 3600 * nominal_fps +
	       (int64_t)tc->minutes * 60 * nominal_fps +
	       (int64_t)tc->seconds * nominal_fps +
	       (int64_t)tc->frames;
}

static void encode_next_frame(struct ltc_source_context *ctx)
{
	if (!ctx->encoder)
		return;

	/* First frame or encoder was reset: must hard-sync */
	if (!ctx->frame_valid) {
		int64_t sec, usec;
		ntp_corrected_time(ctx->ntp_offset_ms, &sec, &usec);

		smpte_timecode_t tc;
		timecode_from_unix(sec, usec, ctx->framerate, &tc);
		ltc_wrapper_set_timecode(ctx->encoder, tc.hours, tc.minutes,
					tc.seconds, tc.frames, tc.year,
					tc.month, tc.day, ctx->camera_id);

#ifdef ENABLE_FRONTEND_API
		{
			char tc_str[16];
			timecode_to_string(&tc, tc_str, sizeof(tc_str));
			metadata_writer_set_timecode(ctx->metadata, tc_str);
		}
#endif

		/* Store sync reference point */
		ctx->sync_ref_sec = sec;
		ctx->sync_ref_usec = usec;
		ctx->sync_ref_frame = ctx->frames_encoded;
		goto encode;
	}

	/* Periodic drift check: compare free-running TC with wall clock */
	if (ctx->frames_encoded % RESYNC_CHECK_FRAMES == 0) {
		int64_t sec, usec;
		ntp_corrected_time(ctx->ntp_offset_ms, &sec, &usec);

		smpte_timecode_t wall_tc;
		timecode_from_unix(sec, usec, ctx->framerate, &wall_tc);
		int64_t wall_frames = tc_to_total_frames(&wall_tc,
							 ctx->nominal_fps);

		/* Calculate where our free-running encoder should be */
		smpte_timecode_t ref_tc;
		timecode_from_unix(ctx->sync_ref_sec, ctx->sync_ref_usec,
				   ctx->framerate, &ref_tc);
		int64_t ref_frames = tc_to_total_frames(&ref_tc,
							ctx->nominal_fps);
		int64_t elapsed = (int64_t)(ctx->frames_encoded -
					    ctx->sync_ref_frame);
		int64_t expected_frames = ref_frames + elapsed;

		/* Handle midnight rollover (24h in frames) */
		int64_t day_frames = (int64_t)24 * 3600 * ctx->nominal_fps;
		int64_t drift = wall_frames - (expected_frames % day_frames);

		/* Normalize drift to [-day_frames/2, day_frames/2] */
		if (drift > day_frames / 2)
			drift -= day_frames;
		else if (drift < -day_frames / 2)
			drift += day_frames;

		if (drift < -RESYNC_DRIFT_THRESHOLD ||
		    drift > RESYNC_DRIFT_THRESHOLD) {
			/* Drift exceeds threshold: hard resync */
			obs_log(LOG_WARNING,
				"LTC timecode drift detected (%lld frames), resyncing",
				(long long)drift);
			ltc_wrapper_set_timecode(ctx->encoder,
						wall_tc.hours,
						wall_tc.minutes,
						wall_tc.seconds,
						wall_tc.frames,
						wall_tc.year,
						wall_tc.month,
						wall_tc.day,
						ctx->camera_id);

			ctx->sync_ref_sec = sec;
			ctx->sync_ref_usec = usec;
			ctx->sync_ref_frame = ctx->frames_encoded;
		} else {
			/* Drift within tolerance: continue free-running */
			ltc_wrapper_inc_timecode(ctx->encoder);
		}
	} else {
		/* Normal operation: increment timecode */
		ltc_wrapper_inc_timecode(ctx->encoder);
	}

encode:
	ctx->frame_samples_total =
		ltc_wrapper_encode_frame(ctx->encoder, ctx->frame_buf,
					MAX_FRAME_SAMPLES);

	if (ctx->frame_samples_total > 0) {
		ctx->frame_valid = true;
		ctx->frame_pos = 0;
		ctx->frames_encoded++;
	} else {
		ctx->frame_valid = false;
	}
}

static const char *ltc_source_get_name(void *unused)
{
	(void)unused;
	return obs_module_text("LTCTimecodeGenerator");
}

static void ltc_source_video_tick(void *data, float seconds)
{
	struct ltc_source_context *ctx = data;
	if (!ctx || !ctx->source)
		return;

	pthread_mutex_lock(&ctx->encoder_mutex);

	if (!ctx->encoder) {
		pthread_mutex_unlock(&ctx->encoder_mutex);
		return;
	}

	int samples_needed = (int)(seconds * SAMPLE_RATE);
	if (samples_needed <= 0)
		samples_needed = SAMPLE_RATE / 30;
	if (samples_needed > AUDIO_BUF_FRAMES)
		samples_needed = AUDIO_BUF_FRAMES;

	int buf_pos = 0;
	while (buf_pos < samples_needed) {
		if (!ctx->frame_valid || ctx->frame_pos >= ctx->frame_samples_total) {
			encode_next_frame(ctx);
			if (!ctx->frame_valid)
				break;
		}

		int remaining_in_frame = ctx->frame_samples_total - ctx->frame_pos;
		int remaining_in_buf = samples_needed - buf_pos;
		int to_copy = remaining_in_frame < remaining_in_buf
				      ? remaining_in_frame
				      : remaining_in_buf;

		memcpy(&ctx->audio_buf[buf_pos],
		       &ctx->frame_buf[ctx->frame_pos],
		       (size_t)to_copy * sizeof(float));

		buf_pos += to_copy;
		ctx->frame_pos += to_copy;
	}

	if (buf_pos < samples_needed) {
		memset(&ctx->audio_buf[buf_pos], 0,
		       (size_t)(samples_needed - buf_pos) * sizeof(float));
	}

	pthread_mutex_unlock(&ctx->encoder_mutex);

	uint64_t now = os_gettime_ns();
	if (ctx->next_audio_ts == 0 ||
	    now > ctx->next_audio_ts + 200000000ULL ||
	    ctx->next_audio_ts > now + 200000000ULL) {
		ctx->next_audio_ts = now;
	}

	ctx->audio_output.data[0] = (uint8_t *)ctx->audio_buf;
	ctx->audio_output.frames = (uint32_t)samples_needed;
	ctx->audio_output.timestamp = ctx->next_audio_ts;
	ctx->audio_output.samples_per_sec = SAMPLE_RATE;
	ctx->audio_output.speakers = SPEAKERS_MONO;
	ctx->audio_output.format = AUDIO_FORMAT_FLOAT;

	obs_source_output_audio(ctx->source, &ctx->audio_output);

	ctx->next_audio_ts += (uint64_t)samples_needed * 1000000000ULL / SAMPLE_RATE;
}

/* ---- OBS source callbacks ---- */

static void *ltc_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct ltc_source_context *ctx = bzalloc(sizeof(struct ltc_source_context));
	ctx->source = source;
	ctx->ntp_offset_ms = 0;
	ctx->ntp_synced = false;
	ctx->first_sync_done = false;
	ctx->next_audio_ts = 0;

	pthread_mutex_init(&ctx->encoder_mutex, NULL);
	memset(&ctx->audio_output, 0, sizeof(ctx->audio_output));

	int fps_setting = (int)obs_data_get_int(settings, S_FRAMERATE);
	if (fps_setting == 0)
		ctx->framerate = detect_obs_framerate();
	else if (fps_setting_valid(fps_setting))
		ctx->framerate = (tc_framerate_t)fps_setting;
	else
		ctx->framerate = TC_FPS_25; /* safe fallback */

	const char *server = obs_data_get_string(settings, S_NTP_SERVER);
	if (server && *server)
		snprintf(ctx->ntp_server, sizeof(ctx->ntp_server), "%s", server);
	else
		snprintf(ctx->ntp_server, sizeof(ctx->ntp_server), "pool.ntp.org");

	ctx->sync_interval_sec = (int)obs_data_get_int(settings, S_SYNC_INTERVAL);
	if (ctx->sync_interval_sec <= 0)
		ctx->sync_interval_sec = 300;

	/* Camera ID for LTC User Bits */
	ctx->camera_id = (int)obs_data_get_int(settings, S_CAMERA_ID);
	if (ctx->camera_id < 0 || ctx->camera_id > 7)
		ctx->camera_id = 0;

	/* Audio track routing */
	ctx->audio_track = (int)obs_data_get_int(settings, S_AUDIO_TRACK);
	if (ctx->audio_track < 1 || ctx->audio_track > 6)
		ctx->audio_track = 3;
	obs_source_set_audio_mixers(source,
				    (uint32_t)(1 << (ctx->audio_track - 1)));

	create_encoder(ctx);
	start_ntp_thread(ctx);

#ifdef ENABLE_FRONTEND_API
	ctx->metadata = metadata_writer_create();
#endif

	obs_log(LOG_INFO,
		"LTC source created (fps=%d, server=%s, interval=%ds, track=%d)",
		ctx->nominal_fps, ctx->ntp_server, ctx->sync_interval_sec,
		ctx->audio_track);

	return ctx;
}

static void ltc_source_destroy(void *data)
{
	struct ltc_source_context *ctx = data;
	if (!ctx)
		return;

	stop_ntp_thread(ctx);

#ifdef ENABLE_FRONTEND_API
	metadata_writer_destroy(ctx->metadata);
#endif

	pthread_mutex_lock(&ctx->encoder_mutex);
	if (ctx->encoder) {
		ltc_wrapper_destroy(ctx->encoder);
		ctx->encoder = NULL;
	}
	pthread_mutex_unlock(&ctx->encoder_mutex);
	pthread_mutex_destroy(&ctx->encoder_mutex);

	obs_log(LOG_INFO, "LTC source destroyed");

	bfree(ctx);
}

static void ltc_source_update(void *data, obs_data_t *settings)
{
	struct ltc_source_context *ctx = data;
	if (!ctx)
		return;

	int fps_setting = (int)obs_data_get_int(settings, S_FRAMERATE);
	tc_framerate_t new_fps;
	if (fps_setting == 0)
		new_fps = detect_obs_framerate();
	else if (fps_setting_valid(fps_setting))
		new_fps = (tc_framerate_t)fps_setting;
	else
		new_fps = ctx->framerate; /* keep current on invalid value */

	if (new_fps != ctx->framerate)
		recreate_encoder(ctx, new_fps);

	const char *server = obs_data_get_string(settings, S_NTP_SERVER);
	bool server_changed = false;
	if (server && *server) {
		pthread_mutex_lock(&ctx->encoder_mutex);
		if (strncmp(ctx->ntp_server, server,
			    sizeof(ctx->ntp_server)) != 0) {
			snprintf(ctx->ntp_server, sizeof(ctx->ntp_server),
				 "%s", server);
			server_changed = true;
		}
		pthread_mutex_unlock(&ctx->encoder_mutex);
	}

	int interval = (int)obs_data_get_int(settings, S_SYNC_INTERVAL);
	if (interval > 0)
		ctx->sync_interval_sec = interval;

	/* Camera ID */
	int new_cam = (int)obs_data_get_int(settings, S_CAMERA_ID);
	if (new_cam >= 0 && new_cam <= 7)
		ctx->camera_id = new_cam;

	/* Audio track routing */
	int new_track = (int)obs_data_get_int(settings, S_AUDIO_TRACK);
	if (new_track >= 1 && new_track <= 6 &&
	    new_track != ctx->audio_track) {
		ctx->audio_track = new_track;
		obs_source_set_audio_mixers(
			ctx->source,
			(uint32_t)(1 << (ctx->audio_track - 1)));
		obs_log(LOG_INFO, "LTC audio track changed to %d",
			ctx->audio_track);
	}

	/* Restart NTP thread to pick up new server immediately */
	if (server_changed) {
		stop_ntp_thread(ctx);
		ctx->first_sync_done = false;
		start_ntp_thread(ctx);
	}

#ifdef ENABLE_FRONTEND_API
	/* Update metadata writer with current settings */
	{
		const char *sync_str = "local";
		switch (ctx->sync_method) {
		case SYNC_METHOD_NTP:
			sync_str = "NTP";
			break;
		case SYNC_METHOD_HTTP:
			sync_str = "HTTP";
			break;
		case SYNC_METHOD_LOCAL:
			sync_str = "local";
			break;
		default:
			break;
		}
		char fps_label[16];
		snprintf(fps_label, sizeof(fps_label), "%d",
			 ctx->nominal_fps);
		metadata_writer_set_info(ctx->metadata, ctx->camera_id,
					fps_label, ctx->ntp_server,
					ctx->ntp_synced,
					ctx->ntp_offset_ms, sync_str);
	}
#endif
}

/* ---- Properties UI ---- */

static obs_properties_t *ltc_source_get_properties(void *data)
{
	struct ltc_source_context *ctx = data;
	obs_properties_t *props = obs_properties_create();

	/* Framerate dropdown */
	obs_property_t *fps_prop = obs_properties_add_list(
		props, S_FRAMERATE, obs_module_text("Framerate"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(fps_prop, obs_module_text("FramerateAuto"), 0);
	obs_property_list_add_int(fps_prop, "24 fps", TC_FPS_24);
	obs_property_list_add_int(fps_prop, "25 fps", TC_FPS_25);
	obs_property_list_add_int(fps_prop, "29.97 fps (Drop-Frame)", TC_FPS_29_97_DF);
	obs_property_list_add_int(fps_prop, "30 fps", TC_FPS_30);
	obs_property_list_add_int(fps_prop, "50 fps", TC_FPS_50);
	obs_property_list_add_int(fps_prop, "60 fps", TC_FPS_60);

	/* NTP server */
	obs_properties_add_text(props, S_NTP_SERVER,
				obs_module_text("NTPServer"), OBS_TEXT_DEFAULT);

	/* Sync interval dropdown */
	obs_property_t *interval_prop = obs_properties_add_list(
		props, S_SYNC_INTERVAL, obs_module_text("NTPSyncInterval"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(interval_prop, "1 min", 60);
	obs_property_list_add_int(interval_prop, "5 min", 300);
	obs_property_list_add_int(interval_prop, "10 min", 600);
	obs_property_list_add_int(interval_prop, "30 min", 1800);

	/* Camera ID dropdown */
	obs_property_t *cam_prop = obs_properties_add_list(
		props, S_CAMERA_ID, obs_module_text("CameraID"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(cam_prop, "Kamera A", 0);
	obs_property_list_add_int(cam_prop, "Kamera B", 1);
	obs_property_list_add_int(cam_prop, "Kamera C", 2);
	obs_property_list_add_int(cam_prop, "Kamera D", 3);
	obs_property_list_add_int(cam_prop, "Kamera E", 4);
	obs_property_list_add_int(cam_prop, "Kamera F", 5);
	obs_property_list_add_int(cam_prop, "Kamera G", 6);
	obs_property_list_add_int(cam_prop, "Kamera H", 7);

	/* Audio track selection */
	obs_property_t *track_prop = obs_properties_add_list(
		props, S_AUDIO_TRACK, obs_module_text("AudioTrack"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(track_prop, "Track 1", 1);
	obs_property_list_add_int(track_prop, "Track 2", 2);
	obs_property_list_add_int(track_prop, "Track 3", 3);
	obs_property_list_add_int(track_prop, "Track 4", 4);
	obs_property_list_add_int(track_prop, "Track 5", 5);
	obs_property_list_add_int(track_prop, "Track 6", 6);

#ifdef ENABLE_FRONTEND_API
	/* Check if selected track is being recorded */
	if (ctx) {
		config_t *profile = obs_frontend_get_profile_config();
		if (profile) {
			uint64_t simple_tracks =
				config_get_uint(profile, "SimpleOutput",
						"RecTracks");
			uint64_t adv_tracks =
				config_get_uint(profile, "AdvOut",
						"RecTracks");
			/* Combine both — user may be in either output mode */
			uint64_t rec_tracks = simple_tracks | adv_tracks;
			uint64_t track_bit =
				(uint64_t)(1 << (ctx->audio_track - 1));

			if (rec_tracks > 0 &&
			    !(rec_tracks & track_bit)) {
				obs_properties_add_text(
					props, "_track_warning",
					obs_module_text("TrackNotRecorded"),
					OBS_TEXT_INFO);
			}
		}
	}
#endif

	/* NTP status (informational) */
	obs_properties_add_text(props, "_ntp_status",
				obs_module_text("NTPStatus"), OBS_TEXT_INFO);

	/* Current timecode display */
	if (ctx) {
		int64_t sec, usec;
		ntp_corrected_time(ctx->ntp_offset_ms, &sec, &usec);
		smpte_timecode_t tc;
		char tc_buf[16];
		timecode_from_unix(sec, usec, ctx->framerate, &tc);
		timecode_to_string(&tc, tc_buf, sizeof(tc_buf));
	}

	obs_properties_add_text(props, "_timecode",
				obs_module_text("CurrentTimecode"), OBS_TEXT_INFO);

	return props;
}

static void ltc_source_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, S_FRAMERATE, 0);
	obs_data_set_default_string(settings, S_NTP_SERVER, "pool.ntp.org");
	obs_data_set_default_int(settings, S_SYNC_INTERVAL, 300);
	obs_data_set_default_int(settings, S_CAMERA_ID, 0);
	obs_data_set_default_int(settings, S_AUDIO_TRACK, 3);
}

/* ---- Source registration ---- */

static struct obs_source_info ltc_source_info = {
	.id = "obs_ltc_timecode_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = ltc_source_get_name,
	.create = ltc_source_create,
	.destroy = ltc_source_destroy,
	.get_properties = ltc_source_get_properties,
	.get_defaults = ltc_source_get_defaults,
	.update = ltc_source_update,
	.video_tick = ltc_source_video_tick,
};

void ltc_source_register(void)
{
	obs_register_source(&ltc_source_info);
}
