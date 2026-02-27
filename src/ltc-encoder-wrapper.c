/*
 * ltc-encoder-wrapper.c - Clean C wrapper around libltc encoder
 */

#include "ltc-encoder-wrapper.h"
#include <ltc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -12dBFS amplitude for LTC signal */
#define LTC_AMPLITUDE 0.25

struct ltc_wrapper {
	LTCEncoder *encoder;
	int sample_rate;
	int fps;
};

ltc_wrapper_t *ltc_wrapper_create(int sample_rate, int fps)
{
	if (sample_rate <= 0 || fps <= 0)
		return NULL;

	ltc_wrapper_t *w = calloc(1, sizeof(ltc_wrapper_t));
	if (!w)
		return NULL;

	w->sample_rate = sample_rate;
	w->fps = fps;

	/* samples_per_frame = sample_rate / fps */
	double spf = (double)sample_rate / (double)fps;

	w->encoder = ltc_encoder_create(sample_rate, spf, LTC_TV_625_50, LTC_USE_DATE);
	if (!w->encoder) {
		free(w);
		return NULL;
	}

	/* Set encoder volume to -12dBFS */
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

int ltc_wrapper_encode_frame(ltc_wrapper_t *w, float *buffer, int max_samples)
{
	if (!w || !w->encoder || !buffer || max_samples <= 0)
		return -1;

	ltc_encoder_encode_frame(w->encoder);

	int len = 0;
	ltcsnd_sample_t *buf = ltc_encoder_get_bufptr(w->encoder, &len, 1);

	if (!buf || len <= 0 || len > max_samples)
		return -1;

	/* Convert from unsigned byte samples to float */
	for (int i = 0; i < len; i++) {
		buffer[i] = ((float)buf[i] - 128.0f) / 128.0f;
	}

	return len;
}
