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
 * mw-recording.c - MW recording status reporting
 *
 * Adds "MW Aufnahme" to OBS Tools menu with a settings dialog.
 * Hooks into OBS recording start/stop to send status to a remote server.
 * All settings stored in OBS user config (global).
 */

#ifdef ENABLE_FRONTEND_API

#include "mw-recording.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <util/platform.h>
#include <util/threading.h>
#include <util/config-file.h>

#include <curl/curl.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#endif

#define MW_HEARTBEAT_INTERVAL_SEC 30
#define MW_CONFIG_SECTION "mw-recording"
#define MW_CONFIG_SERVER_URL "server_url"
#define MW_CONFIG_USER_NAME "user_name"
#define MW_CONFIG_API_KEY "api_key"
#define MW_CONFIG_CAMERA_ID "camera_id"
#define MW_CONFIG_ENABLED "enabled"
#define MW_CONFIG_CONSENT "consent_given"

/* ---- Global state ---- */

static struct {
	pthread_mutex_t mutex;
	char server_url[512];
	char user_name[100];
	char api_key[256];
	int camera_id;
	bool enabled;
	bool consent_given;

	bool recording_active;

	pthread_t heartbeat_thread;
	os_event_t *stop_event;
	bool thread_created;
} g_mw;

static bool g_initialized = false;

/* ---- Config helpers ---- */

static void load_config(void)
{
	config_t *config = obs_frontend_get_user_config();
	if (!config)
		return;

	pthread_mutex_lock(&g_mw.mutex);

	const char *url = config_get_string(config, MW_CONFIG_SECTION, MW_CONFIG_SERVER_URL);
	const char *name = config_get_string(config, MW_CONFIG_SECTION, MW_CONFIG_USER_NAME);
	const char *key = config_get_string(config, MW_CONFIG_SECTION, MW_CONFIG_API_KEY);

	if (url)
		snprintf(g_mw.server_url, sizeof(g_mw.server_url), "%s", url);
	if (name)
		snprintf(g_mw.user_name, sizeof(g_mw.user_name), "%s", name);
	if (key)
		snprintf(g_mw.api_key, sizeof(g_mw.api_key), "%s", key);

	g_mw.camera_id = (int)config_get_int(config, MW_CONFIG_SECTION, MW_CONFIG_CAMERA_ID);
	g_mw.enabled = config_get_bool(config, MW_CONFIG_SECTION, MW_CONFIG_ENABLED);
	g_mw.consent_given = config_get_bool(config, MW_CONFIG_SECTION, MW_CONFIG_CONSENT);

	pthread_mutex_unlock(&g_mw.mutex);
}

static void save_config(void)
{
	config_t *config = obs_frontend_get_user_config();
	if (!config)
		return;

	pthread_mutex_lock(&g_mw.mutex);

	config_set_string(config, MW_CONFIG_SECTION, MW_CONFIG_SERVER_URL, g_mw.server_url);
	config_set_string(config, MW_CONFIG_SECTION, MW_CONFIG_USER_NAME, g_mw.user_name);
	config_set_string(config, MW_CONFIG_SECTION, MW_CONFIG_API_KEY, g_mw.api_key);
	config_set_int(config, MW_CONFIG_SECTION, MW_CONFIG_CAMERA_ID, g_mw.camera_id);
	config_set_bool(config, MW_CONFIG_SECTION, MW_CONFIG_ENABLED, g_mw.enabled);
	config_set_bool(config, MW_CONFIG_SECTION, MW_CONFIG_CONSENT, g_mw.consent_given);

	pthread_mutex_unlock(&g_mw.mutex);

	config_save(config);
}

/* ---- curl helpers ---- */

static size_t discard_write(char *ptr, size_t size, size_t nmemb, void *data)
{
	(void)ptr;
	(void)data;
	return size * nmemb;
}

static bool mw_http_post(const char *base_url, const char *url_suffix, const char *api_key, const char *json_body)
{
	if (!base_url || !base_url[0])
		return false;

	char url[1024];
	snprintf(url, sizeof(url), "%s/api.php%s", base_url, url_suffix);

	CURL *curl = curl_easy_init();
	if (!curl)
		return false;

	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");

	char key_header[300];
	snprintf(key_header, sizeof(key_header), "X-API-Key: %s", api_key);
	headers = curl_slist_append(headers, key_header);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_write);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

	CURLcode res = curl_easy_perform(curl);

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		obs_log(LOG_WARNING, "MW HTTP POST failed: %s (url: %s)", curl_easy_strerror(res), url);
		return false;
	}

	return true;
}

/* ---- Heartbeat thread ---- */

static void *heartbeat_thread_func(void *data)
{
	(void)data;
	os_set_thread_name("mw-heartbeat");

	while (os_event_timedwait(g_mw.stop_event, 0) != 0) {
		char server[512];
		char name[100];
		char key[256];

		pthread_mutex_lock(&g_mw.mutex);
		snprintf(server, sizeof(server), "%s", g_mw.server_url);
		snprintf(name, sizeof(name), "%s", g_mw.user_name);
		snprintf(key, sizeof(key), "%s", g_mw.api_key);
		bool active = g_mw.recording_active;
		pthread_mutex_unlock(&g_mw.mutex);

		if (active && server[0] && name[0]) {
			char body[256];
			snprintf(body, sizeof(body), "{\"name\":\"%s\"}", name);
			mw_http_post(server, "?action=heartbeat", key, body);
			obs_log(LOG_DEBUG, "MW heartbeat sent for '%s'", name);
		}

		if (os_event_timedwait(g_mw.stop_event, MW_HEARTBEAT_INTERVAL_SEC * 1000) == 0)
			return NULL;
	}

	return NULL;
}

static void start_heartbeat_thread(void)
{
	if (g_mw.thread_created)
		return;

	if (os_event_init(&g_mw.stop_event, OS_EVENT_TYPE_MANUAL) != 0)
		return;

	if (pthread_create(&g_mw.heartbeat_thread, NULL, heartbeat_thread_func, NULL) == 0) {
		g_mw.thread_created = true;
	} else {
		os_event_destroy(g_mw.stop_event);
		g_mw.stop_event = NULL;
	}
}

static void stop_heartbeat_thread(void)
{
	if (!g_mw.thread_created)
		return;

	os_event_signal(g_mw.stop_event);
	pthread_join(g_mw.heartbeat_thread, NULL);
	os_event_destroy(g_mw.stop_event);
	g_mw.stop_event = NULL;
	g_mw.thread_created = false;
}

/* ---- Recording start/stop ---- */

static void mw_send_start(void)
{
	char server[512], name[100], key[256];
	int cam;

	pthread_mutex_lock(&g_mw.mutex);
	snprintf(server, sizeof(server), "%s", g_mw.server_url);
	snprintf(name, sizeof(name), "%s", g_mw.user_name);
	snprintf(key, sizeof(key), "%s", g_mw.api_key);
	cam = g_mw.camera_id;
	pthread_mutex_unlock(&g_mw.mutex);

	if (!server[0] || !name[0]) {
		obs_log(LOG_WARNING, "MW recording: server URL or name not configured");
		return;
	}

	char body[512];
	snprintf(body, sizeof(body), "{\"name\":\"%s\",\"camera_id\":\"%c\"}", name, 'A' + cam);
	mw_http_post(server, "?action=start", key, body);
	obs_log(LOG_INFO, "MW recording started: '%s' camera %c", name, 'A' + cam);
}

static void mw_send_stop(void)
{
	char server[512], name[100], key[256];
	int cam;

	pthread_mutex_lock(&g_mw.mutex);
	snprintf(server, sizeof(server), "%s", g_mw.server_url);
	snprintf(name, sizeof(name), "%s", g_mw.user_name);
	snprintf(key, sizeof(key), "%s", g_mw.api_key);
	cam = g_mw.camera_id;
	pthread_mutex_unlock(&g_mw.mutex);

	if (!server[0] || !name[0])
		return;

	char body[512];
	snprintf(body, sizeof(body), "{\"name\":\"%s\",\"camera_id\":\"%c\"}", name, 'A' + cam);
	mw_http_post(server, "?action=stop", key, body);
	obs_log(LOG_INFO, "MW recording stopped: '%s' camera %c", name, 'A' + cam);
}

/* ---- Consent handling ---- */

#ifdef _WIN32
static bool show_consent_dialog(const char *server_url)
{
	char msg[1024];
	snprintf(msg, sizeof(msg),
		 "MW Aufnahme - Datenschutz (DSGVO)\n\n"
		 "Durch die MW-Aufnahme werden folgende Daten an den "
		 "Server uebermittelt:\n\n"
		 "  - Dein Anzeigename\n"
		 "  - Deine Kamera-ID (A-H)\n"
		 "  - Aufnahmestatus (online/offline)\n"
		 "  - Zeitstempel (Start, Stop, Heartbeat)\n\n"
		 "NUR der Regisseur kann diese Daten einsehen.\n"
		 "Es werden KEINE Audio-, Video- oder Bilddaten "
		 "uebertragen.\n\n"
		 "Sessions werden nach 30 Tagen automatisch geloescht.\n"
		 "Du kannst deine Einwilligung jederzeit widerrufen.\n\n"
		 "Server: %s\n"
		 "Datenschutzerklaerung: %s/datenschutz.php\n\n"
		 "Bist du damit einverstanden?",
		 server_url, server_url);

	int result = MessageBoxA(NULL, msg, "MW Aufnahme - DSGVO Einwilligung",
				 MB_YESNO | MB_ICONQUESTION | MB_SYSTEMMODAL);
	return result == IDYES;
}
#endif

static bool ensure_consent(void)
{
	if (g_mw.consent_given)
		return true;

#ifdef _WIN32
	char server[512], name[100], key[256];

	pthread_mutex_lock(&g_mw.mutex);
	snprintf(server, sizeof(server), "%s", g_mw.server_url);
	snprintf(name, sizeof(name), "%s", g_mw.user_name);
	snprintf(key, sizeof(key), "%s", g_mw.api_key);
	pthread_mutex_unlock(&g_mw.mutex);

	if (show_consent_dialog(server)) {
		g_mw.consent_given = true;
		save_config();

		char body[512];
		snprintf(body, sizeof(body), "{\"name\":\"%s\",\"consent\":true}", name);
		mw_http_post(server, "?action=consent", key, body);

		obs_log(LOG_INFO, "MW recording: user '%s' gave consent", name);
		return true;
	}

	obs_log(LOG_INFO, "MW recording: user declined consent");
	return false;
#else
	obs_log(LOG_WARNING, "MW recording consent dialog not available on this platform");
	return false;
#endif
}

/* ---- Frontend event callback ---- */

static void on_frontend_event(enum obs_frontend_event event, void *data)
{
	(void)data;

	if (!g_initialized || !g_mw.enabled)
		return;

	if (event == OBS_FRONTEND_EVENT_RECORDING_STARTING) {
		if (!ensure_consent()) {
			obs_log(LOG_INFO, "MW recording: skipping (no consent)");
			return;
		}
	}

	if (event == OBS_FRONTEND_EVENT_RECORDING_STARTED) {
		if (!g_mw.consent_given)
			return;

		pthread_mutex_lock(&g_mw.mutex);
		g_mw.recording_active = true;
		pthread_mutex_unlock(&g_mw.mutex);

		mw_send_start();
		start_heartbeat_thread();
	}

	if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
		pthread_mutex_lock(&g_mw.mutex);
		bool was_active = g_mw.recording_active;
		g_mw.recording_active = false;
		pthread_mutex_unlock(&g_mw.mutex);

		if (was_active) {
			stop_heartbeat_thread();
			mw_send_stop();
		}
	}

	if (event == OBS_FRONTEND_EVENT_RECORDING_PAUSED) {
#ifdef _WIN32
		if (g_mw.recording_active) {
			MessageBoxA(NULL,
				    "Achtung: Die Aufnahme wurde pausiert!\n\n"
				    "Das Pausieren der Aufnahme kann den "
				    "Timecode-Sync zerstoeren.\n\n"
				    "Bitte die Aufnahme nicht pausieren, "
				    "sondern stoppen und neu starten.",
				    "MW Aufnahme - Warnung",
				    MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL);
		}
#endif
	}
}

/* ---- Win32 Settings Dialog ---- */

#ifdef _WIN32

#define IDC_SERVER_URL 1001
#define IDC_USER_NAME 1002
#define IDC_API_KEY 1003
#define IDC_CAMERA_ID 1004
#define IDC_ENABLED 1005
#define IDC_SAVE 1006
#define IDC_RESET_CONSENT 1007

static HWND g_settings_hwnd = NULL;

static LRESULT CALLBACK settings_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE: {
		HFONT hFont = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
					  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
		HFONT hFontBold = CreateFontA(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
					      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

		int y = 15;
		int lbl_x = 20, edit_x = 160, w = 280, h = 24;

		/* Title */
		HWND title = CreateWindowA("STATIC", "MW Aufnahme - Einstellungen", WS_CHILD | WS_VISIBLE | SS_LEFT,
					   lbl_x, y, 420, 28, hwnd, NULL, NULL, NULL);
		SendMessageA(title, WM_SETFONT, (WPARAM)hFontBold, TRUE);
		y += 40;

		/* Enabled checkbox */
		HWND chk = CreateWindowA("BUTTON", "MW Aufnahme aktivieren",
					 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, lbl_x, y, 300, h, hwnd,
					 (HMENU)(INT_PTR)IDC_ENABLED, NULL, NULL);
		SendMessageA(chk, WM_SETFONT, (WPARAM)hFont, TRUE);
		y += 35;

		/* Server URL */
		HWND lbl1 = CreateWindowA("STATIC", "Server URL:", WS_CHILD | WS_VISIBLE | SS_LEFT, lbl_x, y + 2,
					  130, h, hwnd, NULL, NULL, NULL);
		SendMessageA(lbl1, WM_SETFONT, (WPARAM)hFont, TRUE);
		HWND ed1 = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
					   WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, edit_x, y, w, h, hwnd,
					   (HMENU)(INT_PTR)IDC_SERVER_URL, NULL, NULL);
		SendMessageA(ed1, WM_SETFONT, (WPARAM)hFont, TRUE);
		y += 32;

		/* Anzeigename */
		HWND lbl2 = CreateWindowA("STATIC", "Anzeigename:", WS_CHILD | WS_VISIBLE | SS_LEFT, lbl_x, y + 2,
					  130, h, hwnd, NULL, NULL, NULL);
		SendMessageA(lbl2, WM_SETFONT, (WPARAM)hFont, TRUE);
		HWND ed2 = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
					   WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, edit_x, y, w, h, hwnd,
					   (HMENU)(INT_PTR)IDC_USER_NAME, NULL, NULL);
		SendMessageA(ed2, WM_SETFONT, (WPARAM)hFont, TRUE);
		y += 32;

		/* API Key */
		HWND lbl3 = CreateWindowA("STATIC", "API Key:", WS_CHILD | WS_VISIBLE | SS_LEFT, lbl_x, y + 2, 130,
					  h, hwnd, NULL, NULL, NULL);
		SendMessageA(lbl3, WM_SETFONT, (WPARAM)hFont, TRUE);
		HWND ed3 = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
					   WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD, edit_x, y, w, h,
					   hwnd, (HMENU)(INT_PTR)IDC_API_KEY, NULL, NULL);
		SendMessageA(ed3, WM_SETFONT, (WPARAM)hFont, TRUE);
		y += 32;

		/* Camera ID */
		HWND lbl4 = CreateWindowA("STATIC", "Kamera:", WS_CHILD | WS_VISIBLE | SS_LEFT, lbl_x, y + 2, 130,
					  h, hwnd, NULL, NULL, NULL);
		SendMessageA(lbl4, WM_SETFONT, (WPARAM)hFont, TRUE);
		HWND combo = CreateWindowA("COMBOBOX", "",
					   WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, edit_x, y, 80,
					   200, hwnd, (HMENU)(INT_PTR)IDC_CAMERA_ID, NULL, NULL);
		SendMessageA(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
		for (int i = 0; i < 8; i++) {
			char cam[4];
			snprintf(cam, sizeof(cam), "%c", 'A' + i);
			SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)cam);
		}
		y += 45;

		/* Save button */
		HWND btn = CreateWindowA("BUTTON", "Speichern", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, edit_x, y,
					 120, 32, hwnd, (HMENU)(INT_PTR)IDC_SAVE, NULL, NULL);
		SendMessageA(btn, WM_SETFONT, (WPARAM)hFontBold, TRUE);

		/* Reset consent button */
		HWND btn2 = CreateWindowA("BUTTON", "DSGVO zuruecksetzen", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
					  edit_x + 130, y, 150, 32, hwnd,
					  (HMENU)(INT_PTR)IDC_RESET_CONSENT, NULL, NULL);
		SendMessageA(btn2, WM_SETFONT, (WPARAM)hFont, TRUE);

		/* Populate fields from config */
		pthread_mutex_lock(&g_mw.mutex);
		SetDlgItemTextA(hwnd, IDC_SERVER_URL, g_mw.server_url);
		SetDlgItemTextA(hwnd, IDC_USER_NAME, g_mw.user_name);
		SetDlgItemTextA(hwnd, IDC_API_KEY, g_mw.api_key);
		SendDlgItemMessageA(hwnd, IDC_CAMERA_ID, CB_SETCURSEL, g_mw.camera_id, 0);
		CheckDlgButton(hwnd, IDC_ENABLED, g_mw.enabled ? BST_CHECKED : BST_UNCHECKED);
		pthread_mutex_unlock(&g_mw.mutex);

		return 0;
	}

	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_SAVE) {
			pthread_mutex_lock(&g_mw.mutex);
			GetDlgItemTextA(hwnd, IDC_SERVER_URL, g_mw.server_url, sizeof(g_mw.server_url));
			GetDlgItemTextA(hwnd, IDC_USER_NAME, g_mw.user_name, sizeof(g_mw.user_name));
			GetDlgItemTextA(hwnd, IDC_API_KEY, g_mw.api_key, sizeof(g_mw.api_key));
			g_mw.camera_id = (int)SendDlgItemMessageA(hwnd, IDC_CAMERA_ID, CB_GETCURSEL, 0, 0);
			if (g_mw.camera_id < 0)
				g_mw.camera_id = 0;
			g_mw.enabled = IsDlgButtonChecked(hwnd, IDC_ENABLED) == BST_CHECKED;
			pthread_mutex_unlock(&g_mw.mutex);

			save_config();
			obs_log(LOG_INFO, "MW recording settings saved (enabled=%d)", g_mw.enabled);
			MessageBoxA(hwnd, "Einstellungen gespeichert!", "MW Aufnahme", MB_OK | MB_ICONINFORMATION);
		}

		if (LOWORD(wParam) == IDC_RESET_CONSENT) {
			int r = MessageBoxA(hwnd,
					    "DSGVO-Einwilligung zuruecksetzen?\n\n"
					    "Beim naechsten Aufnahmestart wirst du "
					    "erneut nach deiner Einwilligung gefragt.",
					    "MW Aufnahme", MB_YESNO | MB_ICONQUESTION);
			if (r == IDYES) {
				g_mw.consent_given = false;
				save_config();
				obs_log(LOG_INFO, "MW recording: consent reset");
				MessageBoxA(hwnd, "Einwilligung zurueckgesetzt.", "MW Aufnahme",
					    MB_OK | MB_ICONINFORMATION);
			}
		}
		return 0;

	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;

	case WM_DESTROY:
		g_settings_hwnd = NULL;
		return 0;
	}

	return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static void open_settings_dialog(void)
{
	if (g_settings_hwnd) {
		SetForegroundWindow(g_settings_hwnd);
		return;
	}

	static bool class_registered = false;
	if (!class_registered) {
		WNDCLASSA wc = {0};
		wc.lpfnWndProc = settings_wnd_proc;
		wc.hInstance = GetModuleHandleA(NULL);
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wc.lpszClassName = "MWRecordingSettings";
		RegisterClassA(&wc);
		class_registered = true;
	}

	g_settings_hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME, "MWRecordingSettings", "MW Aufnahme",
					  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
					  480, 340, NULL, NULL, GetModuleHandleA(NULL), NULL);

	ShowWindow(g_settings_hwnd, SW_SHOW);
	UpdateWindow(g_settings_hwnd);
}

#endif /* _WIN32 */

/* ---- Tools menu callback ---- */

static void on_tools_menu_clicked(void *data)
{
	(void)data;
#ifdef _WIN32
	open_settings_dialog();
#else
	obs_log(LOG_INFO, "MW recording settings dialog not available on this platform");
#endif
}

/* ---- Public API ---- */

void mw_recording_init(void)
{
	memset(&g_mw, 0, sizeof(g_mw));
	pthread_mutex_init(&g_mw.mutex, NULL);

	load_config();

	obs_frontend_add_tools_menu_item("MW Aufnahme", on_tools_menu_clicked, NULL);
	obs_frontend_add_event_callback(on_frontend_event, NULL);

	g_initialized = true;
	obs_log(LOG_INFO, "MW recording initialized (enabled=%d)", g_mw.enabled);
}

void mw_recording_cleanup(void)
{
	g_initialized = false;

	stop_heartbeat_thread();

	if (g_mw.recording_active) {
		g_mw.recording_active = false;
		mw_send_stop();
	}

	obs_frontend_remove_event_callback(on_frontend_event, NULL);

#ifdef _WIN32
	if (g_settings_hwnd) {
		DestroyWindow(g_settings_hwnd);
		g_settings_hwnd = NULL;
	}
#endif

	pthread_mutex_destroy(&g_mw.mutex);
}

#endif /* ENABLE_FRONTEND_API */
