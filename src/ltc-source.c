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
 *   - video_tick runs on OBS video thread: reads ntp_target_offset_ms,
 *     owns ntp_offset_ms_applied, drives the LTC encoder
 *   - NTP sync thread: writes ntp_target_offset_ms + raw + age periodically
 *     and on resync-event wakeup
 *   - update runs on OBS main thread: may recreate encoder (protected by mutex)
 */

#include "ltc-source.h"
#include "ntp-client.h"
#include "http-time-client.h"
#include "timecode.h"
#include "ltc-encoder-wrapper.h"
#ifdef ENABLE_FRONTEND_API
#include "metadata-writer.h"
#include "mw-recording.h"
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
#define HTTP_FALLBACK_URL "https://www.google.com"
#define HTTP_FALLBACK_TIMEOUT_MS 5000

/* Built-in NTP fallbacks: tried in order after the user-configured server.
 * Both are widely-deployed anycast services that route through different
 * networks than pool.ntp.org — so if the operator's home router or ISP is
 * blocking the user-configured server, one of these usually still reaches a
 * stratum-1 source. */
static const char *NTP_FALLBACK_SERVERS[] = {
	"time.cloudflare.com",
	"time.google.com",
};
#define NTP_FALLBACK_COUNT (sizeof(NTP_FALLBACK_SERVERS) / sizeof(NTP_FALLBACK_SERVERS[0]))

/* Slewing: how fast the applied offset chases the latest raw measurement.
 * 1 ms / encoded LTC frame @ 25 fps = 25 ppm — within hardware LTC generator
 * tolerance (±50 ppm), so DaVinci Resolve syncs cleanly. 10 ms/frame when no
 * recording is active gives sub-minute catch-up after a 60 s clock jump. */
#define SLEW_MS_PER_FRAME_RECORDING 1
#define SLEW_MS_PER_FRAME_IDLE 10

/* Degraded-state log throttle. First failure logs immediately; further
 * failures log at most once per 60 s. */
#define DEGRADED_LOG_INTERVAL_NS (60ULL * 1000000000ULL)

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
	os_event_t *resync_event; /* signalled by ltc_source_kick_resync() */
	bool thread_created;
	char ntp_server[256];
	int sync_interval_sec;

	/* NTP target offset: latest raw measurement from NTP thread.
	 * Single writer (NTP thread), readers are the encoder + accessors. */
	volatile int64_t ntp_target_offset_ms;

	/* Applied offset: what encode_next_frame actually uses each frame.
	 * Single writer (video thread, in encode_next_frame), single reader
	 * (same thread). Slewed toward ntp_target_offset_ms. */
	int64_t ntp_offset_ms_applied;

	/* Last raw measurement + when it arrived — exposed to the dashboard
	 * via the offset accessor so directors see actual sync quality, not
	 * the slewed value. */
	volatile int64_t ntp_last_raw_offset_ms;
	volatile uint64_t ntp_last_sync_ns;

	volatile bool ntp_synced;
	volatile int64_t ntp_roundtrip_ms;
	volatile sync_method_t sync_method;
	bool first_sync_done;

	/* Degradation tracking (NTP thread only) */
	int consecutive_sync_failures;
	uint64_t last_degraded_log_ns;

	/* Edge tracking so the encoder can apply target instantly on the first
	 * frame after the NTP thread recovers from a failure (only when not
	 * recording — TICKET-036 contract is preserved). */
	volatile bool ntp_sync_recovered_edge;

	/* Timecode re-sync tracking */
	uint64_t frames_encoded;

	/* Audio track routing */
	int audio_track; /* 1-6, default 3 */

	/* Camera identification for LTC User Bits */
	int camera_id; /* 0-15 maps to A-P */

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

	/* Whether this cycle was triggered by the resync_event (director kick)
	 * versus the regular interval timer. Set in the wait loop below; reset
	 * here on the very first iteration (timer-driven). */
	bool cycle_kicked_by_resync = false;

	while (os_event_timedwait(ctx->stop_event, 0) != 0) {
		ntp_result_t result;
		bool success = false;
		sync_method_t method = SYNC_METHOD_NTP;

		/* Copy server name under lock to avoid data race with update */
		char server_copy[256];
		pthread_mutex_lock(&ctx->encoder_mutex);
		snprintf(server_copy, sizeof(server_copy), "%s",
			 ctx->ntp_server);
		pthread_mutex_unlock(&ctx->encoder_mutex);

		/* 1) User-configured NTP server */
		for (int attempt = 0; attempt < NTP_RETRY_COUNT; attempt++) {
			if (os_event_try(ctx->stop_event) == 0)
				return NULL;

			if (ntp_query(server_copy, NTP_QUERY_TIMEOUT_MS,
				      &result)) {
				success = true;
				break;
			}
		}

		/* 2) Built-in NTP anycast fallbacks */
		for (size_t s = 0; s < NTP_FALLBACK_COUNT && !success; s++) {
			const char *fb = NTP_FALLBACK_SERVERS[s];
			if (strcmp(fb, server_copy) == 0)
				continue; /* already tried via user-config */
			for (int attempt = 0; attempt < NTP_RETRY_COUNT;
			     attempt++) {
				if (os_event_try(ctx->stop_event) == 0)
					return NULL;
				if (ntp_query(fb, NTP_QUERY_TIMEOUT_MS,
					      &result)) {
					success = true;
					obs_log(LOG_INFO,
						"NTP fallback succeeded via %s (user-server '%s' unreachable)",
						fb, server_copy);
					break;
				}
			}
		}

		/* 3) HTTP Date header fallback (last resort with a real source) */
		if (!success) {
			http_time_result_t http_result;
			if (http_time_query(HTTP_FALLBACK_URL,
					    HTTP_FALLBACK_TIMEOUT_MS,
					    &http_result)) {
				result.offset_ms = http_result.offset_ms;
				result.roundtrip_ms = 0;
				success = true;
				method = SYNC_METHOD_HTTP;
				obs_log(LOG_WARNING,
					"All NTP servers failed; using HTTP Date header fallback (~1s accuracy)");
			}
		}

		/* 4) Apply or degrade */
		if (success) {
			bool was_degraded =
				(ctx->consecutive_sync_failures > 0) ||
				!ctx->ntp_synced;
			ctx->ntp_target_offset_ms = result.offset_ms;
			ctx->ntp_last_raw_offset_ms = result.offset_ms;
			ctx->ntp_last_sync_ns = os_gettime_ns();
			ctx->ntp_roundtrip_ms = result.roundtrip_ms;
			ctx->ntp_synced = true;
			ctx->sync_method = method;
			ctx->consecutive_sync_failures = 0;
			if (!ctx->first_sync_done) {
				ctx->first_sync_done = true;
				/* Initial sync — encoder will pick this up via
				 * the !frame_valid path. */
			} else if (was_degraded || cycle_kicked_by_resync) {
				/* Recovered from failure OR director-issued
				 * resync: ask the encoder to apply target
				 * instantly on the next frame (only honoured
				 * when not recording — TICKET-036 contract). */
				ctx->ntp_sync_recovered_edge = true;
				if (was_degraded)
					obs_log(LOG_INFO,
						"Time sync restored via %s",
						method == SYNC_METHOD_NTP
							? "NTP"
							: "HTTP fallback");
			}
		} else {
			ctx->consecutive_sync_failures++;
			ctx->ntp_synced = false;
			ctx->sync_method = SYNC_METHOD_LOCAL;
			uint64_t now_ns = os_gettime_ns();
			if (ctx->consecutive_sync_failures == 1 ||
			    now_ns - ctx->last_degraded_log_ns >=
				    DEGRADED_LOG_INTERVAL_NS) {
				obs_log(LOG_ERROR,
					"Time sync UNAVAILABLE — Timecode is drifting on the local clock. "
					"Tried user='%s', time.cloudflare.com, time.google.com, HTTP Date header. "
					"Check network/firewall (UDP 123 outbound, HTTPS).",
					server_copy);
				ctx->last_degraded_log_ns = now_ns;
			}
			/* After 3 consecutive full-chain failures, treat the
			 * next success as an initial sync so the encoder
			 * applies the target directly instead of slewing from
			 * a stale value. */
			if (ctx->consecutive_sync_failures >= 3)
				ctx->first_sync_done = false;
		}

		/*
		 * Sleep up to sync_interval_sec but wake immediately if either
		 * stop_event or resync_event fires. Poll in 500ms chunks —
		 * negligible CPU cost; worst-case 500ms latency on a director-
		 * issued resync command, which is well within tolerance.
		 */
		unsigned long total_ms =
			(unsigned long)ctx->sync_interval_sec * 1000UL;
		unsigned long elapsed_ms = 0;
		cycle_kicked_by_resync = false;
		while (elapsed_ms < total_ms) {
			unsigned long chunk_ms = 500;
			if (chunk_ms > total_ms - elapsed_ms)
				chunk_ms = total_ms - elapsed_ms;

			if (os_event_timedwait(ctx->stop_event, chunk_ms) == 0)
				return NULL;

			/* AUTO event: try consumes-and-resets in one shot */
			if (ctx->resync_event &&
			    os_event_try(ctx->resync_event) == 0) {
				obs_log(LOG_INFO,
					"LTC NTP thread kicked by remote resync");
				cycle_kicked_by_resync = true;
				break; /* go around to re-run NTP query */
			}

			elapsed_ms += chunk_ms;
		}
	}

	return NULL;
}

static void start_ntp_thread(struct ltc_source_context *ctx)
{
	if (ctx->thread_created)
		return;

	if (os_event_init(&ctx->stop_event, OS_EVENT_TYPE_MANUAL) != 0)
		return;
	if (os_event_init(&ctx->resync_event, OS_EVENT_TYPE_AUTO) != 0) {
		os_event_destroy(ctx->stop_event);
		ctx->stop_event = NULL;
		return;
	}

	if (pthread_create(&ctx->ntp_thread, NULL, ntp_sync_thread, ctx) == 0) {
		ctx->thread_created = true;
	} else {
		os_event_destroy(ctx->stop_event);
		os_event_destroy(ctx->resync_event);
		ctx->stop_event = NULL;
		ctx->resync_event = NULL;
	}
}

static void stop_ntp_thread(struct ltc_source_context *ctx)
{
	if (!ctx->thread_created)
		return;

	os_event_signal(ctx->stop_event);
	pthread_join(ctx->ntp_thread, NULL);
	os_event_destroy(ctx->stop_event);
	if (ctx->resync_event) {
		os_event_destroy(ctx->resync_event);
		ctx->resync_event = NULL;
	}
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
 * Encode one LTC frame.
 *
 * Each frame's timecode is recomputed from wall-clock + applied NTP offset.
 * The applied offset is slewed toward the latest NTP target measurement at
 * a rate chosen by recording state — small steps during a recording (so the
 * TC sequence stays monotonic and within hardware LTC generator tolerance),
 * larger steps when idle (fast catch-up).
 *
 * This deliberately calls set_timecode every frame instead of using libltc's
 * inc_timecode + a periodic drift correction. The earlier approach
 * (TICKET-008) still permitted a hard-set inside an active recording on
 * clock perturbations; slewing makes that impossible by design.
 */
static void encode_next_frame(struct ltc_source_context *ctx)
{
	if (!ctx->encoder)
		return;

	bool initial_sync = (!ctx->frame_valid) || (!ctx->first_sync_done);

	/* On NTP recovery after a failure, when no recording is active, jump
	 * to the new target instantly (matches the director's expectation
	 * after clicking "Re-sync now"). TICKET-036 already prevents the
	 * kick path from delivering a resync while recording, so this only
	 * fires in safe states. */
	bool instant_recover = false;
#ifdef ENABLE_FRONTEND_API
	if (ctx->ntp_sync_recovered_edge && !obs_frontend_recording_active()) {
		instant_recover = true;
	}
	ctx->ntp_sync_recovered_edge = false;
#else
	if (ctx->ntp_sync_recovered_edge)
		instant_recover = true;
	ctx->ntp_sync_recovered_edge = false;
#endif

	if (initial_sync || instant_recover) {
		ctx->ntp_offset_ms_applied = ctx->ntp_target_offset_ms;
	} else {
		int64_t step;
#ifdef ENABLE_FRONTEND_API
		step = obs_frontend_recording_active()
			       ? SLEW_MS_PER_FRAME_RECORDING
			       : SLEW_MS_PER_FRAME_IDLE;
#else
		step = SLEW_MS_PER_FRAME_IDLE;
#endif
		ctx->ntp_offset_ms_applied = ntp_slew_step(
			ctx->ntp_offset_ms_applied,
			ctx->ntp_target_offset_ms, step);
	}

	int64_t sec, usec;
	ntp_corrected_time(ctx->ntp_offset_ms_applied, &sec, &usec);

	smpte_timecode_t tc;
	timecode_from_unix(sec, usec, ctx->framerate, &tc);
	ltc_wrapper_set_timecode(ctx->encoder, tc.hours, tc.minutes,
				 tc.seconds, tc.frames, tc.year, tc.month,
				 tc.day, ctx->camera_id);

#ifdef ENABLE_FRONTEND_API
	{
		char tc_str[16];
		timecode_to_string(&tc, tc_str, sizeof(tc_str));
		metadata_writer_set_timecode(ctx->metadata, tc_str);
	}
#endif

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
	ctx->ntp_target_offset_ms = 0;
	ctx->ntp_offset_ms_applied = 0;
	ctx->ntp_last_raw_offset_ms = 0;
	ctx->ntp_last_sync_ns = 0;
	ctx->ntp_synced = false;
	ctx->first_sync_done = false;
	ctx->consecutive_sync_failures = 0;
	ctx->last_degraded_log_ns = 0;
	ctx->ntp_sync_recovered_edge = false;
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

	/* Camera ID for LTC User Bits.
	 * If not explicitly set in source settings and MW recording has a
	 * configured camera ID, inherit from MW. Existing sources keep their
	 * stored value (upgrade-safe). */
	bool has_cam_setting = obs_data_has_user_value(settings, S_CAMERA_ID);
	ctx->camera_id = (int)obs_data_get_int(settings, S_CAMERA_ID);
	if (ctx->camera_id < 0 || ctx->camera_id > 15)
		ctx->camera_id = 0;
#ifdef ENABLE_FRONTEND_API
	if (!has_cam_setting) {
		int mw_cam = mw_recording_get_camera_id();
		if (mw_cam >= 0 && mw_cam <= 15) {
			ctx->camera_id = mw_cam;
			obs_data_set_int(settings, S_CAMERA_ID, mw_cam);
		}
	}
#else
	(void)has_cam_setting;
#endif

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
	if (new_cam >= 0 && new_cam <= 15) {
		bool cam_changed = (ctx->camera_id != new_cam);
		ctx->camera_id = new_cam;
#ifdef ENABLE_FRONTEND_API
		/* Sync to MW Aufnahme (propagate_to_ltc=true updates other
		 * LTC sources; the current source already has the value). */
		if (cam_changed)
			mw_recording_set_camera_id(new_cam, true);
#else
		(void)cam_changed;
#endif
	}

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
					ctx->ntp_last_raw_offset_ms, sync_str);
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

	/* Camera ID dropdown (A-P, 16 values — fits in LTC user7 4-bit field) */
	obs_property_t *cam_prop = obs_properties_add_list(
		props, S_CAMERA_ID, obs_module_text("CameraID"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (int i = 0; i < 16; i++) {
		char label[16];
		snprintf(label, sizeof(label), "Kamera %c", 'A' + i);
		obs_property_list_add_int(cam_prop, label, i);
	}

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

	/* Degraded-sync banner (only when the NTP chain + HTTP fallback have
	 * all failed). Shown above the status line so non-technical operators
	 * can't miss it. */
	if (ctx && !ctx->ntp_synced && ctx->consecutive_sync_failures > 0) {
		obs_properties_add_text(props, "_ntp_degraded_warning",
					obs_module_text("NTPDegradedWarning"),
					OBS_TEXT_INFO);
	}

	/* NTP status (informational) */
	obs_properties_add_text(props, "_ntp_status",
				obs_module_text("NTPStatus"), OBS_TEXT_INFO);

	/* Current timecode display */
	if (ctx) {
		int64_t sec, usec;
		ntp_corrected_time(ctx->ntp_offset_ms_applied, &sec, &usec);
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

/* ---- Cross-module accessor: read offset from first LTC source ---- */

struct offset_accessor_state {
	bool found;
	int64_t offset_ms;
	int sync_method;
	bool synced;
	int64_t raw_offset_ms;
	int offset_age_sec;
};

static bool offset_accessor_cb(void *data, obs_source_t *source)
{
	struct offset_accessor_state *st = data;
	if (!source || st->found)
		return true;

	const char *src_id = obs_source_get_id(source);
	if (!src_id || strcmp(src_id, "obs_ltc_timecode_source") != 0)
		return true;

	struct ltc_source_context *ctx =
		(struct ltc_source_context *)obs_obj_get_data(source);
	if (!ctx)
		return true;

	/* Report the latest RAW measurement (not the slewed applied value).
	 * Directors need to see actual sync quality — slewing only changes
	 * how fast the encoder absorbs the new measurement, not the
	 * measurement itself. */
	st->offset_ms = ctx->ntp_last_raw_offset_ms;
	st->raw_offset_ms = ctx->ntp_last_raw_offset_ms;
	st->sync_method = (int)ctx->sync_method;
	st->synced = ctx->ntp_synced;
	uint64_t last_ns = ctx->ntp_last_sync_ns;
	if (last_ns == 0) {
		st->offset_age_sec = -1; /* never synced */
	} else {
		uint64_t now_ns = os_gettime_ns();
		uint64_t age_ns = (now_ns > last_ns) ? (now_ns - last_ns) : 0;
		st->offset_age_sec = (int)(age_ns / 1000000000ULL);
	}
	st->found = true;
	return false; /* stop enumeration */
}

bool ltc_source_get_current_offset(int64_t *offset_ms, int *sync_method,
				   bool *synced, int64_t *raw_offset_ms,
				   int *offset_age_sec)
{
	struct offset_accessor_state st = {0};
	obs_enum_sources(offset_accessor_cb, &st);

	if (!st.found) {
		if (offset_ms)
			*offset_ms = 0;
		if (sync_method)
			*sync_method = (int)SYNC_METHOD_NONE;
		if (synced)
			*synced = false;
		if (raw_offset_ms)
			*raw_offset_ms = 0;
		if (offset_age_sec)
			*offset_age_sec = -1;
		return false;
	}

	if (offset_ms)
		*offset_ms = st.offset_ms;
	if (sync_method)
		*sync_method = st.sync_method;
	if (synced)
		*synced = st.synced;
	if (raw_offset_ms)
		*raw_offset_ms = st.raw_offset_ms;
	if (offset_age_sec)
		*offset_age_sec = st.offset_age_sec;
	return true;
}

/* ---- Cross-module trigger: kick every LTC source's NTP thread ---- */

static bool kick_resync_cb(void *data, obs_source_t *source)
{
	int *count = data;
	if (!source)
		return true;

	const char *src_id = obs_source_get_id(source);
	if (!src_id || strcmp(src_id, "obs_ltc_timecode_source") != 0)
		return true;

	struct ltc_source_context *ctx =
		(struct ltc_source_context *)obs_obj_get_data(source);
	if (!ctx || !ctx->resync_event)
		return true;

	os_event_signal(ctx->resync_event);
	(*count)++;
	return true;
}

int ltc_source_kick_resync(void)
{
	int count = 0;
	obs_enum_sources(kick_resync_cb, &count);
	return count;
}

void ltc_source_register(void)
{
	obs_register_source(&ltc_source_info);
}
