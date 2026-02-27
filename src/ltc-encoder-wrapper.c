/*
 * ltc-encoder-wrapper.c - Clean C wrapper around libltc encoder
 */

#include "ltc-encoder-wrapper.h"
#include <ltc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ltc_wrapper {
	LTCEncoder *encoder;
	int sample_rate;
	int nominal_fps;
	enum LTC_TV_STANDARD tv_standard;
};

ltc_wrapper_t *ltc_wrapper_create(int sample_rate, tc_framerate_t fps)
{
	if (sample_rate <= 0)
		return NULL;

	double fps_rate;
	int nominal_fps;
	enum LTC_TV_STANDARD tv_std;

	switch (fps) {
	case TC_FPS_24:
		fps_rate = 24.0;
		nominal_fps = 24;
		tv_std = LTC_TV_FILM_24;
		break;
	case TC_FPS_25:
		fps_rate = 25.0;
		nominal_fps = 25;
		tv_std = LTC_TV_625_50;
		break;
	case TC_FPS_29_97_DF:
		fps_rate = 30000.0 / 1001.0;
		nominal_fps = 30;
		tv_std = LTC_TV_525_60;
		break;
	case TC_FPS_30:
		fps_rate = 30.0;
		nominal_fps = 30;
		tv_std = LTC_TV_525_60;
		break;
	case TC_FPS_50:
		fps_rate = 50.0;
		nominal_fps = 50;
		tv_std = LTC_TV_625_50;
		break;
	case TC_FPS_60:
		fps_rate = 60.0;
		nominal_fps = 60;
		tv_std = LTC_TV_525_60;
		break;
	default:
		return NULL;
	}

	ltc_wrapper_t *w = calloc(1, sizeof(ltc_wrapper_t));
	if (!w)
		return NULL;

	w->sample_rate = sample_rate;
	w->nominal_fps = nominal_fps;
	w->tv_standard = tv_std;

	w->encoder = ltc_encoder_create((double)sample_rate, fps_rate, tv_std, LTC_USE_DATE);
	if (!w->encoder) {
		free(w);
		return NULL;
	}

	ltc_encoder_set_volume(w->encoder, -12.0);

	return w;
}

void ltc_wrapper_destroy(ltc_wrapper_t *w)
{
	if (!w)
		return;

	if (w->encoder)
		ltc_encoder_free(w->encoder);

	free(w);
}

void ltc_wrapper_set_timecode(ltc_wrapper_t *w, int h, int m, int s, int f)
{
	if (!w || !w->encoder)
		return;

	SMPTETimecode st;
	memset(&st, 0, sizeof(st));

	snprintf(st.timezone, sizeof(st.timezone), "+0000");
	st.years = 0;
	st.months = 0;
	st.days = 0;
	st.hours = h;
	st.mins = m;
	st.secs = s;
	st.frame = f;

	ltc_encoder_set_timecode(w->encoder, &st);
}

void ltc_wrapper_inc_timecode(ltc_wrapper_t *w)
{
	if (!w || !w->encoder)
		return;
	ltc_encoder_inc_timecode(w->encoder);
}

int ltc_wrapper_encode_frame(ltc_wrapper_t *w, float *buffer, int max_samples)
{
	if (!w || !w->encoder || !buffer || max_samples <= 0)
		return -1;

	ltc_encoder_encode_frame(w->encoder);

	ltcsnd_sample_t *buf = NULL;
	int len = ltc_encoder_get_bufferptr(w->encoder, &buf, 1);
	if (!buf || len <= 0 || len > max_samples)
		return -1;

	/* Convert from unsigned byte samples to float */
	for (int i = 0; i < len; i++) {
		buffer[i] = ((float)buf[i] - 128.0f) / 128.0f;
	}

	return len;
}

int ltc_wrapper_get_samples_per_frame(ltc_wrapper_t *w)
{
	if (!w)
		return 0;
	return w->sample_rate / w->nominal_fps;
}

int ltc_wrapper_get_fps(ltc_wrapper_t *w)
{
	return w ? w->nominal_fps : 0;
}
