/*
 * ltc-source.c - OBS audio source for LTC timecode output
 *
 * Implements the OBS source callbacks: create, destroy, get_name,
 * get_properties, get_defaults, update, and audio output via timer.
 *
 * Currently outputs silence as proof of life (TICKET-001 skeleton).
 */

#include "ltc-source.h"
#include <obs-module.h>
#include <util/platform.h>

#include <string.h>
#include <stdlib.h>

#define SAMPLE_RATE 48000
#define NUM_CHANNELS 1
#define AUDIO_BUF_FRAMES 4800 /* 100ms at 48kHz */

struct ltc_source_context {
	obs_source_t *source;

	/* Audio buffer (silence for now) */
	float audio_buf[AUDIO_BUF_FRAMES];
	struct obs_source_audio audio_output;
};

static const char *ltc_source_get_name(void *unused)
{
	(void)unused;
	return obs_module_text("LTCTimecodeGenerator");
}

/*
 * video_tick callback - called every video frame by OBS.
 * Used to output silence (or later: LTC audio) to the audio pipeline.
 */
static void ltc_source_video_tick(void *data, float seconds)
{
	(void)seconds;
	struct ltc_source_context *ctx = data;
	if (!ctx || !ctx->source)
		return;

	/* Output silence (zero-filled buffer) */
	memset(ctx->audio_buf, 0, sizeof(ctx->audio_buf));

	ctx->audio_output.data[0] = (uint8_t *)ctx->audio_buf;
	ctx->audio_output.frames = AUDIO_BUF_FRAMES;
	ctx->audio_output.timestamp = os_gettime_ns();
	ctx->audio_output.samples_per_sec = SAMPLE_RATE;
	ctx->audio_output.speakers = SPEAKERS_MONO;
	ctx->audio_output.format = AUDIO_FORMAT_FLOAT;

	obs_source_output_audio(ctx->source, &ctx->audio_output);
}

static void *ltc_source_create(obs_data_t *settings, obs_source_t *source)
{
	(void)settings;

	struct ltc_source_context *ctx = bzalloc(sizeof(struct ltc_source_context));
	ctx->source = source;

	memset(ctx->audio_buf, 0, sizeof(ctx->audio_buf));
	memset(&ctx->audio_output, 0, sizeof(ctx->audio_output));

	return ctx;
}

static void ltc_source_destroy(void *data)
{
	struct ltc_source_context *ctx = data;
	if (!ctx)
		return;

	bfree(ctx);
}

static obs_properties_t *ltc_source_get_properties(void *data)
{
	(void)data;

	obs_properties_t *props = obs_properties_create();

	/* Placeholder: properties will be added in TICKET-007 */

	return props;
}

static void ltc_source_get_defaults(obs_data_t *settings)
{
	(void)settings;
	/* Defaults will be added in TICKET-007 */
}

static void ltc_source_update(void *data, obs_data_t *settings)
{
	(void)data;
	(void)settings;
	/* Update logic will be added in TICKET-007 */
}

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
