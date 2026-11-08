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
	uint64_t source_limit_ms;
	uint64_t start_ns;
	os_event_t *stop_event;
	pthread_t thread;
	bool running;
} g_am;

static uint64_t am_now_ms(void)
{
	return (os_gettime_ns() - g_am.start_ns) / AM_NS_PER_MS;
}

/* ---- Warning overlay ----
 *
 * A big red panel on the monitor OBS is on, always on top. A MessageBox from
 * a background thread opens behind the game and goes unnoticed (field test
 * 2026-11-02). The panel never takes the focus, so the game stays active.
 *
 * Runs on its own module-lifetime thread: neither the monitor thread nor a
 * frontend callback may ever wait on the operator (TICKET-055/056 deadlock). */

#ifdef _WIN32

#define AM_OVERLAY_CLASS L"MwAudioWarningOverlay"
#define AM_OVERLAY_QUEUE 8
#define AM_OVERLAY_TIMER_ID 1
#define AM_OVERLAY_TIMER_MS 500
#define AM_OVERLAY_OK_ID 1
#define AM_OVERLAY_BASE_WIDTH 900
#define AM_OVERLAY_BASE_HEIGHT 420
#define AM_OVERLAY_BORDER 8
#define AM_COLOR_RED RGB(255, 40, 40)
#define AM_COLOR_WHITE RGB(255, 255, 255)
#define AM_COLOR_GREEN RGB(90, 220, 110)

struct am_warning {
	wchar_t title[64];
	wchar_t subject[256];
	wchar_t reason[128];
	char source[128];
	int limit_sec;
	uint64_t warned_ns;
	int frozen_sec; /* -1 while the outage is running */
	bool recovered;
	bool is_ltc;
};

static struct {
	struct am_warning queue[AM_OVERLAY_QUEUE];
	size_t head;
	size_t count;
	struct am_warning current;
	pthread_mutex_t mutex;
	os_event_t *wake_event;
	pthread_t thread;
	bool thread_created;
	volatile bool stopping;
	volatile bool exited;
	volatile HWND hwnd;
	HWND anchor;
	HMODULE module;
	HFONT button_font;
	double scale;
} g_overlay;

static int warning_seconds(const struct am_warning *warning)
{
	if (warning->frozen_sec >= 0)
		return warning->frozen_sec;
	return warning->limit_sec + (int)((os_gettime_ns() - warning->warned_ns) / 1000000000ULL);
}

static HFONT create_overlay_font(int base_height, int weight)
{
	return CreateFontW(-(int)(base_height * g_overlay.scale), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

static void draw_overlay_text(HDC dc, const RECT *area, LONG *top, const wchar_t *text, int base_height, int weight,
			      COLORREF color)
{
	HFONT font = create_overlay_font(base_height, weight);
	HFONT old_font = SelectObject(dc, font);
	RECT line = {area->left, *top, area->right, area->bottom};

	SetTextColor(dc, color);
	DrawTextW(dc, text, -1, &line, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
	line.left = area->left;
	line.right = area->right;
	DrawTextW(dc, text, -1, &line, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
	*top = line.bottom + (LONG)(10 * g_overlay.scale);

	SelectObject(dc, old_font);
	DeleteObject(font);
}

static void paint_overlay(HWND hwnd)
{
	struct am_warning warning;
	PAINTSTRUCT ps;
	RECT client;
	wchar_t status[128];

	pthread_mutex_lock(&g_overlay.mutex);
	warning = g_overlay.current;
	pthread_mutex_unlock(&g_overlay.mutex);

	int seconds = warning_seconds(&warning);
	if (warning.recovered)
		swprintf(status, 128, L"%d:%02d Min. kein Ton erkannt, jetzt wieder da", seconds / 60, seconds % 60);
	else
		swprintf(status, 128, L"Seit %d:%02d Min. kein Ton erkannt", seconds / 60, seconds % 60);

	HDC window_dc = BeginPaint(hwnd, &ps);
	GetClientRect(hwnd, &client);

	/* Drawn off-screen: the panel repaints twice a second for the clock */
	HDC dc = CreateCompatibleDC(window_dc);
	HBITMAP bitmap = CreateCompatibleBitmap(window_dc, client.right, client.bottom);
	HBITMAP old_bitmap = SelectObject(dc, bitmap);
	HBRUSH red = CreateSolidBrush(AM_COLOR_RED);
	HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
	int border = (int)(AM_OVERLAY_BORDER * g_overlay.scale);
	RECT inner = {border, border, client.right - border, client.bottom - border};

	FillRect(dc, &client, red);
	FillRect(dc, &inner, black);
	SetBkMode(dc, TRANSPARENT);

	RECT area = {inner.left + border, inner.top + border, inner.right - border, inner.bottom};
	LONG top = area.top;
	draw_overlay_text(dc, &area, &top, warning.title, 46, FW_BLACK, AM_COLOR_RED);
	draw_overlay_text(dc, &area, &top, warning.subject, 30, FW_BOLD, AM_COLOR_WHITE);
	if (warning.reason[0])
		draw_overlay_text(dc, &area, &top, warning.reason, 26, FW_BOLD, AM_COLOR_WHITE);
	draw_overlay_text(dc, &area, &top, status, 34, FW_BOLD, warning.recovered ? AM_COLOR_GREEN : AM_COLOR_RED);
	draw_overlay_text(dc, &area, &top,
			  L"Melde dich jetzt kurz beim Dreh, damit das gefixt wird.\n"
			  L"Ist das eine Fehlanzeige und der Ton ist in Ordnung? Sag beim Dreh trotzdem Bescheid.",
			  22, FW_NORMAL, AM_COLOR_WHITE);
	if (!warning.is_ltc)
		draw_overlay_text(dc, &area, &top,
				  L"Warnung für diese Quelle abschalten: Werkzeuge → MW Aufnahme → Ton-Warnung", 18,
				  FW_NORMAL, AM_COLOR_WHITE);

	BitBlt(window_dc, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);

	DeleteObject(red);
	DeleteObject(black);
	SelectObject(dc, old_bitmap);
	DeleteObject(bitmap);
	DeleteDC(dc);
	EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK overlay_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
		paint_overlay(hwnd);
		return 0;
	case WM_TIMER:
		/* Other topmost windows may have moved above the panel */
		SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		InvalidateRect(hwnd, NULL, FALSE);
		return 0;
	case WM_COMMAND:
		if (LOWORD(wparam) == AM_OVERLAY_OK_ID)
			DestroyWindow(hwnd);
		return 0;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		KillTimer(hwnd, AM_OVERLAY_TIMER_ID);
		g_overlay.hwnd = NULL;
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(hwnd, msg, wparam, lparam);
	}
}

static void show_overlay_window(void)
{
	MONITORINFO info = {.cbSize = sizeof(info)};
	HMONITOR monitor = MonitorFromWindow(g_overlay.anchor, MONITOR_DEFAULTTOPRIMARY);
	if (!GetMonitorInfoW(monitor, &info))
		return;

	int monitor_width = info.rcMonitor.right - info.rcMonitor.left;
	int monitor_height = info.rcMonitor.bottom - info.rcMonitor.top;
	g_overlay.scale = (double)monitor_width * 0.6 / AM_OVERLAY_BASE_WIDTH;

	int width = (int)(AM_OVERLAY_BASE_WIDTH * g_overlay.scale);
	int height = (int)(AM_OVERLAY_BASE_HEIGHT * g_overlay.scale);
	int x = info.rcMonitor.left + (monitor_width - width) / 2;
	int y = info.rcMonitor.top + (monitor_height - height) / 4;

	HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, AM_OVERLAY_CLASS,
				    L"MW Aufnahme - Ton-Warnung", WS_POPUP, x, y, width, height, NULL, NULL,
				    g_overlay.module, NULL);
	if (!hwnd) {
		obs_log(LOG_ERROR, "audio monitor: overlay window creation failed (%lu)", GetLastError());
		return;
	}

	int button_width = (int)(180 * g_overlay.scale);
	int button_height = (int)(46 * g_overlay.scale);
	HWND button = CreateWindowExW(WS_EX_NOACTIVATE, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
				      (width - button_width) / 2, height - button_height - (int)(24 * g_overlay.scale),
				      button_width, button_height, hwnd, (HMENU)(INT_PTR)AM_OVERLAY_OK_ID,
				      g_overlay.module, NULL);
	g_overlay.button_font = create_overlay_font(24, FW_BOLD);
	SendMessageW(button, WM_SETFONT, (WPARAM)g_overlay.button_font, TRUE);

	g_overlay.hwnd = hwnd;
	SetTimer(hwnd, AM_OVERLAY_TIMER_ID, AM_OVERLAY_TIMER_MS, NULL);
	ShowWindow(hwnd, SW_SHOWNOACTIVATE);
	MessageBeep(MB_ICONWARNING);

	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	DeleteObject(g_overlay.button_font);
	g_overlay.button_font = NULL;
}

static void *overlay_thread_func(void *data)
{
	(void)data;
	os_set_thread_name("mw-audio-warning");

	WNDCLASSW wc = {0};
	wc.lpfnWndProc = overlay_proc;
	wc.hInstance = g_overlay.module;
	wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
	wc.lpszClassName = AM_OVERLAY_CLASS;
	RegisterClassW(&wc);

	while (os_event_wait(g_overlay.wake_event) == 0 && !os_atomic_load_bool(&g_overlay.stopping)) {
		for (;;) {
			pthread_mutex_lock(&g_overlay.mutex);
			bool have_warning = g_overlay.count > 0;
			if (have_warning) {
				g_overlay.current = g_overlay.queue[g_overlay.head];
				g_overlay.head = (g_overlay.head + 1) % AM_OVERLAY_QUEUE;
				g_overlay.count--;
			}
			pthread_mutex_unlock(&g_overlay.mutex);

			if (!have_warning || os_atomic_load_bool(&g_overlay.stopping))
				break;

			show_overlay_window();
		}
	}

	UnregisterClassW(AM_OVERLAY_CLASS, g_overlay.module);
	os_atomic_set_bool(&g_overlay.exited, true);
	return NULL;
}

static void overlay_start(void)
{
	pthread_mutex_init(&g_overlay.mutex, NULL);
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			   (LPCWSTR)overlay_proc, &g_overlay.module);

	if (os_event_init(&g_overlay.wake_event, OS_EVENT_TYPE_AUTO) != 0) {
		obs_log(LOG_ERROR, "audio monitor: wake event creation failed, warnings go to the log only");
		return;
	}

	if (pthread_create(&g_overlay.thread, NULL, overlay_thread_func, NULL) == 0) {
		g_overlay.thread_created = true;
	} else {
		os_event_destroy(g_overlay.wake_event);
		g_overlay.wake_event = NULL;
		obs_log(LOG_ERROR, "audio monitor: overlay thread creation failed, warnings go to the log only");
	}
}

static void overlay_stop(void)
{
	if (g_overlay.thread_created) {
		os_atomic_set_bool(&g_overlay.stopping, true);
		os_event_signal(g_overlay.wake_event);

		/* A warning may still be open: unloading the module under a
		 * live window would crash, so close it first. */
		while (!os_atomic_load_bool(&g_overlay.exited)) {
			HWND hwnd = g_overlay.hwnd;
			if (hwnd)
				PostMessageW(hwnd, WM_CLOSE, 0, 0);
			os_sleep_ms(20);
		}

		pthread_join(g_overlay.thread, NULL);
		os_event_destroy(g_overlay.wake_event);
		g_overlay.wake_event = NULL;
		g_overlay.thread_created = false;
	}

	pthread_mutex_destroy(&g_overlay.mutex);
}

static void overlay_set_anchor(void *main_window)
{
	g_overlay.anchor = main_window;
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

static void overlay_show(const struct am_entry *entry, enum ltc_fault fault)
{
	struct am_warning warning = {0};
	wchar_t name[128] = L"";
	wchar_t track[96] = L"";

	os_utf8_to_wcs(entry->name, 0, name, sizeof(name) / sizeof(name[0]));
	os_utf8_to_wcs(entry->track, 0, track, sizeof(track) / sizeof(track[0]));

	wcscpy_s(warning.title, 64, entry->is_ltc ? L"KEIN TIMECODE IN DER AUFNAHME!" : L"KEIN TON IN DER AUFNAHME!");
	swprintf(warning.subject, 256, L"%ls (%ls)", name, track);
	if (entry->is_ltc)
		wcscpy_s(warning.reason, 128, ltc_fault_text(fault));

	snprintf(warning.source, sizeof(warning.source), "%s", entry->name);
	warning.limit_sec = (int)((entry->is_ltc ? AM_LIMIT_LTC_MS : g_am.source_limit_ms) / 1000);
	warning.warned_ns = os_gettime_ns();
	warning.frozen_sec = -1;
	warning.is_ltc = entry->is_ltc;

	if (!g_overlay.thread_created)
		return;

	pthread_mutex_lock(&g_overlay.mutex);
	if (g_overlay.count < AM_OVERLAY_QUEUE) {
		g_overlay.queue[(g_overlay.head + g_overlay.count) % AM_OVERLAY_QUEUE] = warning;
		g_overlay.count++;
	}
	pthread_mutex_unlock(&g_overlay.mutex);

	os_event_signal(g_overlay.wake_event);
}

static void freeze_warning(struct am_warning *warning, const char *source, bool recovered)
{
	if (warning->frozen_sec >= 0 || (source && strcmp(warning->source, source) != 0))
		return;
	warning->frozen_sec = warning_seconds(warning);
	warning->recovered = recovered;
}

/* Stops the clock: when the sound came back, or for all open warnings when
 * the recording ended. NULL source means all. */
static void overlay_freeze(const char *source, bool recovered)
{
	if (!g_overlay.thread_created)
		return;

	pthread_mutex_lock(&g_overlay.mutex);
	if (g_overlay.current.title[0])
		freeze_warning(&g_overlay.current, source, recovered);
	for (size_t i = 0; i < g_overlay.count; i++)
		freeze_warning(&g_overlay.queue[(g_overlay.head + i) % AM_OVERLAY_QUEUE], source, recovered);
	pthread_mutex_unlock(&g_overlay.mutex);
}

#else

static void overlay_start(void) {}
static void overlay_stop(void) {}
static void overlay_set_anchor(void *main_window)
{
	(void)main_window;
}
static void overlay_show(const struct am_entry *entry, enum ltc_fault fault)
{
	(void)entry;
	(void)fault;
}
static void overlay_freeze(const char *source, bool recovered)
{
	(void)source;
	(void)recovered;
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

	enum am_event event = am_state_update(&entry->state, am_now_ms(),
					      (uint64_t)os_atomic_load_long(&entry->last_signal_ms), ignored,
					      fault != LTC_FAULT_NONE,
					      entry->is_ltc ? AM_LIMIT_LTC_MS : g_am.source_limit_ms);

	if (event == AM_EVENT_WARN) {
		obs_log(LOG_WARNING, "audio monitor: no sound on '%s' (%s), ltc fault %d", entry->name, entry->track,
			(int)fault);
		overlay_show(entry, fault);
	} else if (event == AM_EVENT_RECOVERED) {
		obs_log(LOG_INFO, "audio monitor: sound is back on '%s'", entry->name);
		overlay_freeze(entry->name, true);
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

static const char *warn_config_key(enum am_kind kind)
{
	switch (kind) {
	case AM_KIND_PROGRAM:
		return "AudioWarnProgram";
	case AM_KIND_GAME:
		return "AudioWarnGame";
	case AM_KIND_MIC:
		return "AudioWarnMic";
	default:
		return NULL;
	}
}

bool audio_monitor_warn_enabled(enum am_kind kind)
{
	if (kind == AM_KIND_LTC)
		return true;

	const char *key = warn_config_key(kind);
	config_t *user_config = obs_frontend_get_user_config();
	if (!key || !user_config || !config_has_user_value(user_config, "obs-ltc-timecode", key))
		return am_kind_warns_by_default(kind);
	return config_get_bool(user_config, "obs-ltc-timecode", key);
}

void audio_monitor_set_warn_enabled(enum am_kind kind, bool enabled)
{
	const char *key = warn_config_key(kind);
	config_t *user_config = obs_frontend_get_user_config();
	if (!key || !user_config)
		return;
	config_set_bool(user_config, "obs-ltc-timecode", key, enabled);
	config_save(user_config);
}

int audio_monitor_warn_seconds(void)
{
	config_t *user_config = obs_frontend_get_user_config();
	int64_t seconds = user_config ? config_get_int(user_config, "obs-ltc-timecode", "AudioWarnSeconds") : 0;
	return (int)(am_source_limit_ms(seconds) / 1000);
}

void audio_monitor_set_warn_seconds(int seconds)
{
	config_t *user_config = obs_frontend_get_user_config();
	if (!user_config)
		return;
	config_set_int(user_config, "obs-ltc-timecode", "AudioWarnSeconds", seconds);
	config_save(user_config);
}

/* game_capture always carries OBS_SOURCE_AUDIO, but only delivers sound
 * with "capture_audio" on. Without this check it would warn on every take. */
static bool is_monitored_type(obs_source_t *source, enum am_kind kind)
{
	if (kind == AM_KIND_NONE || kind == AM_KIND_LTC || !audio_monitor_warn_enabled(kind))
		return false;

	if (kind == AM_KIND_GAME) {
		obs_data_t *settings = obs_source_get_settings(source);
		bool capture_audio = obs_data_get_bool(settings, "capture_audio");
		obs_data_release(settings);
		return capture_audio;
	}

	return true;
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
		char key[32];
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

	enum am_kind kind = am_kind_from_id(id);
	bool is_ltc = kind == AM_KIND_LTC;
	if (!is_ltc && !is_monitored_type(source, kind))
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

	overlay_set_anchor(obs_frontend_get_main_window_handle());

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
	g_am.source_limit_ms = (uint64_t)audio_monitor_warn_seconds() * 1000u;

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
	overlay_freeze(NULL, false);

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
	overlay_start();
	obs_frontend_add_event_callback(on_frontend_event, NULL);
}

void audio_monitor_cleanup(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, NULL);
	monitor_stop();
	overlay_stop();
}

#endif /* ENABLE_FRONTEND_API */
