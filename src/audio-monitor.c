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
 * audio-monitor.c - Warns when a source delivers no sound while recording
 *
 * Read-only by design (TICKET-081): the capture callback gets const data
 * after OBS has already queued the audio, and nothing here writes to a
 * source. The LTC path must stay untouched, the monitor only listens.
 */

#ifdef ENABLE_FRONTEND_API

#include "audio-monitor.h"
#include "audio-monitor-core.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <util/platform.h>
#include <util/threading.h>
#include <util/config-file.h>

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#endif

#define LTC_SOURCE_ID "obs_ltc_timecode_source"
#define AM_MAX_SOURCES 32
#define AM_TICK_MS 1000
#define AM_NS_PER_MS 1000000ULL

enum ltc_fault {
	LTC_FAULT_NONE,
	LTC_FAULT_MUTED,
	LTC_FAULT_NOT_RECORDED,
	LTC_FAULT_INACTIVE,
	LTC_FAULT_VOLUME_ZERO,
};

struct am_entry {
	obs_source_t *source;
	char name[128];
	char track[96];
	bool is_ltc;
	volatile long last_signal_ms;
	struct am_source_state state;
};

static struct {
	struct am_entry entries[AM_MAX_SOURCES];
	size_t count;
	bool have_ltc;
	uint32_t rec_tracks;
	uint64_t start_ns;
	os_event_t *stop_event;
	pthread_t thread;
	bool running;
} g_am;

static uint64_t am_now_ms(void)
{
	return (os_gettime_ns() - g_am.start_ns) / AM_NS_PER_MS;
}

/* ---- Warning dialog ----
 *
 * MessageBoxW blocks, so it gets its own module-lifetime thread: neither the
 * monitor thread nor a frontend callback may ever wait on the operator
 * (TICKET-055/056 deadlock). */

#ifdef _WIN32

#define AM_DIALOG_TITLE L"MW Aufnahme – Ton-Warnung"
#define AM_DIALOG_QUEUE 8
#define AM_DIALOG_TEXT_LEN 512

static struct {
	wchar_t queue[AM_DIALOG_QUEUE][AM_DIALOG_TEXT_LEN];
	size_t head;
	size_t count;
	pthread_mutex_t mutex;
	os_event_t *wake_event;
	pthread_t thread;
	bool thread_created;
	volatile bool stopping;
	volatile bool exited;
} g_dialog;

static void *dialog_thread_func(void *data)
{
	(void)data;
	os_set_thread_name("mw-audio-warning");

	while (os_event_wait(g_dialog.wake_event) == 0 && !os_atomic_load_bool(&g_dialog.stopping)) {
		for (;;) {
			wchar_t text[AM_DIALOG_TEXT_LEN];

			pthread_mutex_lock(&g_dialog.mutex);
			bool have_text = g_dialog.count > 0;
			if (have_text) {
				wcscpy_s(text, AM_DIALOG_TEXT_LEN, g_dialog.queue[g_dialog.head]);
				g_dialog.head = (g_dialog.head + 1) % AM_DIALOG_QUEUE;
				g_dialog.count--;
			}
			pthread_mutex_unlock(&g_dialog.mutex);

			if (!have_text || os_atomic_load_bool(&g_dialog.stopping))
				break;

			MessageBoxW(NULL, text, AM_DIALOG_TITLE,
				    MB_OK | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
		}
	}

	os_atomic_set_bool(&g_dialog.exited, true);
	return NULL;
}

static void dialog_start(void)
{
	pthread_mutex_init(&g_dialog.mutex, NULL);

	if (os_event_init(&g_dialog.wake_event, OS_EVENT_TYPE_AUTO) != 0) {
		obs_log(LOG_ERROR, "audio monitor: wake event creation failed, warnings go to the log only");
		return;
	}

	if (pthread_create(&g_dialog.thread, NULL, dialog_thread_func, NULL) == 0) {
		g_dialog.thread_created = true;
	} else {
		os_event_destroy(g_dialog.wake_event);
		g_dialog.wake_event = NULL;
		obs_log(LOG_ERROR, "audio monitor: dialog thread creation failed, warnings go to the log only");
	}
}

static void dialog_stop(void)
{
	if (g_dialog.thread_created) {
		os_atomic_set_bool(&g_dialog.stopping, true);
		os_event_signal(g_dialog.wake_event);

		/* A warning may still be open: unloading the module under a
		 * running MessageBoxW would crash, so close it first. */
		while (!os_atomic_load_bool(&g_dialog.exited)) {
			HWND box = FindWindowW(L"#32770", AM_DIALOG_TITLE);
			if (box)
				PostMessageW(box, WM_CLOSE, 0, 0);
			os_sleep_ms(20);
		}

		pthread_join(g_dialog.thread, NULL);
		os_event_destroy(g_dialog.wake_event);
		g_dialog.wake_event = NULL;
		g_dialog.thread_created = false;
	}

	pthread_mutex_destroy(&g_dialog.mutex);
}

static void dialog_push(const wchar_t *text)
{
	if (!g_dialog.thread_created)
		return;

	pthread_mutex_lock(&g_dialog.mutex);
	if (g_dialog.count < AM_DIALOG_QUEUE) {
		size_t tail = (g_dialog.head + g_dialog.count) % AM_DIALOG_QUEUE;
		wcscpy_s(g_dialog.queue[tail], AM_DIALOG_TEXT_LEN, text);
		g_dialog.count++;
	}
	pthread_mutex_unlock(&g_dialog.mutex);

	os_event_signal(g_dialog.wake_event);
}

static const wchar_t *ltc_fault_text(enum ltc_fault fault)
{
	switch (fault) {
	case LTC_FAULT_MUTED:
		return L"Die Quelle ist stumm geschaltet.";
	case LTC_FAULT_NOT_RECORDED:
		return L"Die Quelle liegt auf keiner aufgenommenen Spur.";
	case LTC_FAULT_INACTIVE:
		return L"Die Quelle ist in der aktuellen Szene nicht aktiv.";
	case LTC_FAULT_VOLUME_ZERO:
		return L"Die Lautstärke der Quelle steht auf 0.";
	default:
		return L"Es kommt kein Signal an.";
	}
}

static void show_warning(const struct am_entry *entry, enum ltc_fault fault)
{
	wchar_t name[128] = L"";
	wchar_t track[96] = L"";
	wchar_t text[AM_DIALOG_TEXT_LEN];

	os_utf8_to_wcs(entry->name, 0, name, sizeof(name) / sizeof(name[0]));
	os_utf8_to_wcs(entry->track, 0, track, sizeof(track) / sizeof(track[0]));

	if (entry->is_ltc) {
		swprintf(text, AM_DIALOG_TEXT_LEN,
			 L"Der LTC-Timecode kommt nicht in der Aufnahme an!\n\n"
			 L"Quelle: %ls\n"
			 L"%ls\n\n"
			 L"Ohne LTC ist diese Aufnahme nicht synchron. Bitte sofort prüfen.",
			 name, ltc_fault_text(fault));
	} else {
		swprintf(text, AM_DIALOG_TEXT_LEN,
			 L"Seit 60 Sekunden kommt kein Ton an:\n\n"
			 L"%ls (%ls)\n\n"
			 L"Bitte prüfen. Wenn das Absicht ist, die Quelle in OBS "
			 L"stumm schalten, dann kommt keine Warnung.",
			 name, track);
	}

	dialog_push(text);
}

#else

static void dialog_start(void) {}
static void dialog_stop(void) {}
static void show_warning(const struct am_entry *entry, enum ltc_fault fault)
{
	(void)entry;
	(void)fault;
}

#endif /* _WIN32 */

/* ---- Audio callback ----
 *
 * Runs on the audio path of the source: stay short, take no locks, never
 * write to the data. */

static void on_source_audio(void *param, obs_source_t *source, const struct audio_data *audio, bool muted)
{
	struct am_entry *entry = param;
	(void)source;
	(void)muted;

	for (size_t ch = 0; ch < MAX_AV_PLANES; ch++) {
		if (am_block_has_signal((const float *)audio->data[ch], audio->frames, AM_SIGNAL_THRESHOLD)) {
			os_atomic_store_long(&entry->last_signal_ms, (long)(am_now_ms() + 1));
			return;
		}
	}
}

/* ---- Monitor thread ---- */

static enum ltc_fault ltc_get_fault(obs_source_t *source, bool on_recorded_track, bool active)
{
	if (obs_source_muted(source))
		return LTC_FAULT_MUTED;
	if (!on_recorded_track)
		return LTC_FAULT_NOT_RECORDED;
	if (!active)
		return LTC_FAULT_INACTIVE;
	if (obs_source_get_volume(source) <= 0.0f)
		return LTC_FAULT_VOLUME_ZERO;
	return LTC_FAULT_NONE;
}

static void check_entry(struct am_entry *entry, bool any_ltc_active)
{
	obs_source_t *source = entry->source;
	bool on_recorded_track = (obs_source_get_audio_mixers(source) & g_am.rec_tracks) != 0;
	bool active = obs_source_active(source);
	enum ltc_fault fault = LTC_FAULT_NONE;
	bool ignored;

	if (entry->is_ltc) {
		/* Only the LTC source that really runs counts; a spare one
		 * outside the scene is no fault. */
		ignored = !active && any_ltc_active;
		fault = ltc_get_fault(source, on_recorded_track, active);
	} else {
		ignored = obs_source_muted(source) || obs_source_get_volume(source) <= 0.0f || !on_recorded_track ||
			  !active;
	}

	enum am_event event =
		am_state_update(&entry->state, am_now_ms(), (uint64_t)os_atomic_load_long(&entry->last_signal_ms),
				ignored, fault != LTC_FAULT_NONE, entry->is_ltc ? AM_LIMIT_LTC_MS : AM_LIMIT_SOURCE_MS);

	if (event == AM_EVENT_WARN) {
		obs_log(LOG_WARNING, "audio monitor: no sound on '%s' (%s), ltc fault %d", entry->name, entry->track,
			(int)fault);
		show_warning(entry, fault);
	} else if (event == AM_EVENT_RECOVERED) {
		obs_log(LOG_INFO, "audio monitor: sound is back on '%s'", entry->name);
	}
}

static void *monitor_thread_func(void *data)
{
	(void)data;
	os_set_thread_name("mw-audio-monitor");

	while (os_event_timedwait(g_am.stop_event, AM_TICK_MS) != 0) {
		bool any_ltc_active = false;

		for (size_t i = 0; i < g_am.count; i++) {
			if (g_am.entries[i].is_ltc && obs_source_active(g_am.entries[i].source))
				any_ltc_active = true;
		}

		for (size_t i = 0; i < g_am.count; i++)
			check_entry(&g_am.entries[i], any_ltc_active);
	}
	return NULL;
}

/* ---- Source collection ---- */

/* game_capture always carries OBS_SOURCE_AUDIO, but only delivers sound
 * with "capture_audio" on. Without this check it would warn on every take. */
static bool is_monitored_type(obs_source_t *source, const char *id)
{
	if (strcmp(id, "wasapi_input_capture") == 0 || strcmp(id, "wasapi_output_capture") == 0 ||
	    strcmp(id, "wasapi_process_output_capture") == 0)
		return true;

	if (strcmp(id, "game_capture") == 0) {
		obs_data_t *settings = obs_source_get_settings(source);
		bool capture_audio = obs_data_get_bool(settings, "capture_audio");
		obs_data_release(settings);
		return capture_audio;
	}

	return false;
}

static void build_track_label(struct am_entry *entry, config_t *profile, bool advanced)
{
	uint32_t routed = obs_source_get_audio_mixers(entry->source) & g_am.rec_tracks;
	const char *track_name = NULL;
	int track = 1;

	if (!routed) {
		snprintf(entry->track, sizeof(entry->track), "keine Spur");
		return;
	}

	while (!(routed & (1u << (track - 1))))
		track++;

	if (profile && advanced) {
		char key[16];
		snprintf(key, sizeof(key), "Track%dName", track);
		track_name = config_get_string(profile, "AdvOut", key);
	}

	if (track_name && *track_name)
		snprintf(entry->track, sizeof(entry->track), "Spur %d, %s", track, track_name);
	else
		snprintf(entry->track, sizeof(entry->track), "Spur %d", track);
}

static bool collect_source(void *param, obs_source_t *source)
{
	(void)param;

	const char *id = obs_source_get_unversioned_id(source);
	if (!id || g_am.count >= AM_MAX_SOURCES)
		return true;

	bool is_ltc = strcmp(id, LTC_SOURCE_ID) == 0;
	if (!is_ltc && !is_monitored_type(source, id))
		return true;

	obs_source_t *ref = obs_source_get_ref(source);
	if (!ref)
		return true;

	struct am_entry *entry = &g_am.entries[g_am.count++];
	memset(entry, 0, sizeof(*entry));
	entry->source = ref;
	entry->is_ltc = is_ltc;
	snprintf(entry->name, sizeof(entry->name), "%s", obs_source_get_name(source));
	g_am.have_ltc = g_am.have_ltc || is_ltc;
	return true;
}

static void release_entries(void)
{
	for (size_t i = 0; i < g_am.count; i++)
		obs_source_release(g_am.entries[i].source);
	g_am.count = 0;
	g_am.have_ltc = false;
}

static void read_recorded_tracks(config_t *profile, bool *advanced)
{
	const char *mode = profile ? config_get_string(profile, "Output", "Mode") : NULL;

	*advanced = mode && strcmp(mode, "Advanced") == 0;
	g_am.rec_tracks =
		profile ? (uint32_t)config_get_uint(profile, *advanced ? "AdvOut" : "SimpleOutput", "RecTracks") : 0;
	if (!g_am.rec_tracks)
		g_am.rec_tracks = 0xFFFFFFFFu;
}

static void monitor_start(void)
{
	if (g_am.running)
		return;

	/* Off switch without a new build: AudioMonitorOff=true in user.ini */
	config_t *user_config = obs_frontend_get_user_config();
	if (user_config && config_get_bool(user_config, "obs-ltc-timecode", "AudioMonitorOff")) {
		obs_log(LOG_INFO, "audio monitor: switched off in user.ini");
		return;
	}

	config_t *profile = obs_frontend_get_profile_config();
	bool advanced = false;

	read_recorded_tracks(profile, &advanced);
	obs_enum_sources(collect_source, NULL);

	/* No LTC in this collection: not an MW setup, stay out of the way. */
	if (!g_am.have_ltc) {
		release_entries();
		return;
	}

	if (os_event_init(&g_am.stop_event, OS_EVENT_TYPE_MANUAL) != 0) {
		obs_log(LOG_ERROR, "audio monitor: stop event creation failed, no monitoring");
		release_entries();
		return;
	}

	g_am.start_ns = os_gettime_ns();

	for (size_t i = 0; i < g_am.count; i++) {
		struct am_entry *entry = &g_am.entries[i];
		build_track_label(entry, profile, advanced);
		am_state_init(&entry->state, 0);
		obs_source_add_audio_capture_callback(entry->source, on_source_audio, entry);
	}

	if (pthread_create(&g_am.thread, NULL, monitor_thread_func, NULL) != 0) {
		obs_log(LOG_ERROR, "audio monitor: thread creation failed, no monitoring");
		for (size_t i = 0; i < g_am.count; i++)
			obs_source_remove_audio_capture_callback(g_am.entries[i].source, on_source_audio,
								 &g_am.entries[i]);
		os_event_destroy(g_am.stop_event);
		g_am.stop_event = NULL;
		release_entries();
		return;
	}

	g_am.running = true;
	obs_log(LOG_INFO, "audio monitor: watching %zu sources", g_am.count);
}

static void monitor_stop(void)
{
	if (!g_am.running)
		return;

	os_event_signal(g_am.stop_event);
	pthread_join(g_am.thread, NULL);

	for (size_t i = 0; i < g_am.count; i++)
		obs_source_remove_audio_capture_callback(g_am.entries[i].source, on_source_audio, &g_am.entries[i]);

	os_event_destroy(g_am.stop_event);
	g_am.stop_event = NULL;
	release_entries();
	g_am.running = false;
}

/* ---- Frontend events ---- */

static void on_frontend_event(enum obs_frontend_event event, void *data)
{
	(void)data;

	if (event == OBS_FRONTEND_EVENT_RECORDING_STARTED)
		monitor_start();
	else if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED || event == OBS_FRONTEND_EVENT_EXIT)
		monitor_stop();
}

/* ---- Public API ---- */

void audio_monitor_init(void)
{
	dialog_start();
	obs_frontend_add_event_callback(on_frontend_event, NULL);
}

void audio_monitor_cleanup(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, NULL);
	monitor_stop();
	dialog_stop();
}

#endif /* ENABLE_FRONTEND_API */
