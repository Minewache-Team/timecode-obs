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
 * audio-monitor-core.c - Pure logic of the audio monitor (TICKET-081)
 */

#include "audio-monitor-core.h"

#include <string.h>

bool am_block_has_signal(const float *samples, size_t count, float threshold)
{
	if (!samples)
		return false;

	for (size_t i = 0; i < count; i++) {
		if (samples[i] > threshold || samples[i] < -threshold)
			return true;
	}
	return false;
}

enum am_kind am_kind_from_id(const char *id)
{
	if (!id)
		return AM_KIND_NONE;
	if (strcmp(id, "obs_ltc_timecode_source") == 0)
		return AM_KIND_LTC;
	if (strcmp(id, "wasapi_process_output_capture") == 0 || strcmp(id, "wasapi_output_capture") == 0)
		return AM_KIND_PROGRAM;
	if (strcmp(id, "game_capture") == 0)
		return AM_KIND_GAME;
	if (strcmp(id, "wasapi_input_capture") == 0)
		return AM_KIND_MIC;
	return AM_KIND_NONE;
}

bool am_kind_warns_by_default(enum am_kind kind)
{
	return kind == AM_KIND_LTC || kind == AM_KIND_PROGRAM || kind == AM_KIND_GAME;
}

uint64_t am_source_limit_ms(int64_t seconds)
{
	if (seconds <= 0)
		return AM_LIMIT_SOURCE_MS;
	if (seconds < AM_LIMIT_SOURCE_MIN_SEC)
		seconds = AM_LIMIT_SOURCE_MIN_SEC;
	if (seconds > AM_LIMIT_SOURCE_MAX_SEC)
		seconds = AM_LIMIT_SOURCE_MAX_SEC;
	return (uint64_t)seconds * 1000u;
}

void am_state_init(struct am_source_state *state, uint64_t now_ms)
{
	state->silent_since_ms = now_ms;
	state->last_seen_signal_ms = 0;
	state->warned = false;
}

enum am_event am_state_update(struct am_source_state *state, uint64_t now_ms, uint64_t last_signal_ms, bool ignored,
			      bool forced_fault, uint64_t limit_ms)
{
	bool fresh_signal = last_signal_ms > state->last_seen_signal_ms;
	if (fresh_signal)
		state->last_seen_signal_ms = last_signal_ms;

	if (ignored) {
		state->silent_since_ms = now_ms;
		state->warned = false;
		return AM_EVENT_NONE;
	}

	if (fresh_signal && !forced_fault) {
		state->silent_since_ms = last_signal_ms;
		if (state->warned) {
			state->warned = false;
			return AM_EVENT_RECOVERED;
		}
		return AM_EVENT_NONE;
	}

	if (!state->warned && now_ms >= state->silent_since_ms && now_ms - state->silent_since_ms >= limit_ms) {
		state->warned = true;
		return AM_EVENT_WARN;
	}
	return AM_EVENT_NONE;
}
