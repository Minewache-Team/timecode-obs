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
 * ltc-encoder-wrapper.h - Clean C wrapper around libltc encoder
 *
 * This module has ZERO OBS dependencies and can be tested standalone.
 */

#ifndef LTC_ENCODER_WRAPPER_H
#define LTC_ENCODER_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "timecode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque encoder context */
typedef struct ltc_wrapper ltc_wrapper_t;

/*
 * Create an LTC encoder for a specific SMPTE framerate.
 * Handles 29.97df correctly (30000/1001 sample rate, dfbit set).
 *
 * @param sample_rate  Audio sample rate (e.g., 48000)
 * @param fps          SMPTE framerate enum
 * @return Encoder context, or NULL on failure
 */
ltc_wrapper_t *ltc_wrapper_create(int sample_rate, tc_framerate_t fps);

/*
 * Destroy an LTC encoder and free resources.
 */
void ltc_wrapper_destroy(ltc_wrapper_t *w);

/*
 * Set the current timecode to encode, including date and camera ID
 * for LTC User Bits (SMPTE 12M).
 *
 * @param w          Encoder context
 * @param h          Hours (0-23)
 * @param m          Minutes (0-59)
 * @param s          Seconds (0-59)
 * @param f          Frames (0 to fps-1)
 * @param year       Two-digit year (0-99)
 * @param month      Month (1-12)
 * @param day        Day (1-31)
 * @param camera_id  Camera identifier (0-15, maps to A-P)
 */
void ltc_wrapper_set_timecode(ltc_wrapper_t *w, int h, int m, int s, int f,
			      int year, int month, int day, int camera_id);

/*
 * Increment the encoder's internal timecode by one frame.
 * Handles midnight rollover and drop-frame skip pattern.
 */
void ltc_wrapper_inc_timecode(ltc_wrapper_t *w);

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

/*
 * Get the nominal samples per LTC frame for this encoder.
 */
int ltc_wrapper_get_samples_per_frame(ltc_wrapper_t *w);

/*
 * Get the nominal integer fps for this encoder.
 */
int ltc_wrapper_get_fps(ltc_wrapper_t *w);

#ifdef __cplusplus
}
#endif

#endif /* LTC_ENCODER_WRAPPER_H */
