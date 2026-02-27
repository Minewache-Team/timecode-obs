/*
 * ltc-encoder-wrapper.h - Clean C wrapper around libltc encoder
 *
 * This module has ZERO OBS dependencies and can be tested standalone.
 */

#ifndef LTC_ENCODER_WRAPPER_H
#define LTC_ENCODER_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque encoder context */
typedef struct ltc_wrapper ltc_wrapper_t;

/*
 * Create an LTC encoder.
 *
 * @param sample_rate  Audio sample rate (e.g., 48000)
 * @param fps          Frame rate (24, 25, 30, 50, 60)
 * @return Encoder context, or NULL on failure
 */
ltc_wrapper_t *ltc_wrapper_create(int sample_rate, int fps);

/*
 * Destroy an LTC encoder and free resources.
 */
void ltc_wrapper_destroy(ltc_wrapper_t *w);

/*
 * Set the current timecode to encode.
 *
 * @param w   Encoder context
 * @param h   Hours (0-23)
 * @param m   Minutes (0-59)
 * @param s   Seconds (0-59)
 * @param f   Frames (0 to fps-1)
 */
void ltc_wrapper_set_timecode(ltc_wrapper_t *w, int h, int m, int s, int f);

/*
 * Encode one LTC frame into PCM audio samples (float, mono).
 * Output amplitude is -12dBFS (0.25).
 *
 * @param w           Encoder context
 * @param buffer      Output buffer for float samples
 * @param max_samples Maximum samples to write
 * @return Number of samples written, or -1 on error
 */
int ltc_wrapper_encode_frame(ltc_wrapper_t *w, float *buffer, int max_samples);

#ifdef __cplusplus
}
#endif

#endif /* LTC_ENCODER_WRAPPER_H */
