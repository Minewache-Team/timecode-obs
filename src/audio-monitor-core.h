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
 * audio-monitor-core.h - Pure logic of the audio monitor (TICKET-081)
 *
 * Free of OBS dependencies so it can be unit-tested without an OBS runtime.
 * Used by audio-monitor.c and tests/test-audio-monitor.cpp.
 */

#ifndef AUDIO_MONITOR_CORE_H
#define AUDIO_MONITOR_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -80 dBFS. Anything quieter counts as "no sound". */
#define AM_SIGNAL_THRESHOLD 0.0001f

#define AM_LIMIT_SOURCE_MS 60000u
#define AM_LIMIT_SOURCE_MIN_SEC 10
#define AM_LIMIT_SOURCE_MAX_SEC 1800
#define AM_LIMIT_LTC_MS 3000u

/* Which sources warn is chosen per kind, not per track: in the kit Discord
 * and the microphone share track 1 (TICKET-084). */
enum am_kind {
	AM_KIND_NONE,
	AM_KIND_LTC,
	AM_KIND_PROGRAM,
	AM_KIND_GAME,
	AM_KIND_MIC,
};

enum am_event {
	AM_EVENT_NONE,
	AM_EVENT_WARN,
	AM_EVENT_RECOVERED,
};

struct am_source_state {
	uint64_t silent_since_ms;
	uint64_t last_seen_signal_ms;
	bool warned;
};

/*
 * True if any sample exceeds the threshold. Stops at the first such sample,
 * so a source with sound only costs a handful of comparisons per block.
 */
bool am_block_has_signal(const float *samples, size_t count, float threshold);

/* Maps an unversioned OBS source id to its kind, AM_KIND_NONE if unwatched.
 * game_capture still needs "capture_audio" checked by the caller. */
enum am_kind am_kind_from_id(const char *id);

/* Default when the user has not chosen: a silent microphone is normal
 * whenever nobody speaks, so it stays off. LTC always warns. */
bool am_kind_warns_by_default(enum am_kind kind);

/* User-set seconds without sound before a non-LTC source warns (TICKET-084).
 * 0 means unset and gives the default, the rest is clamped to 10 s - 30 min. */
uint64_t am_source_limit_ms(int64_t seconds);

void am_state_init(struct am_source_state *state, uint64_t now_ms);

/*
 * Advance one source by one monitor tick.
 *
 * last_signal_ms: time of the newest block with sound, 0 if there was none.
 * ignored:        deliberately off (e.g. muted): no warning, clock restarts.
 * forced_fault:   counts as silent even if samples arrive (LTC muted, not on
 *                 a recorded track, ...).
 *
 * Returns AM_EVENT_WARN once per outage, AM_EVENT_RECOVERED when sound
 * returns after a warning, AM_EVENT_NONE otherwise.
 */
enum am_event am_state_update(struct am_source_state *state, uint64_t now_ms, uint64_t last_signal_ms, bool ignored,
			      bool forced_fault, uint64_t limit_ms);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_MONITOR_CORE_H */
