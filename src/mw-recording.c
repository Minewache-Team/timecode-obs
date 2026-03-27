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
 * All Win32 UI uses Unicode (W) APIs for proper character rendering.
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
#include <shellapi.h>
#pragma comment(lib, "comctl32.lib")
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

/* ================================================================
 * Win32 Dark-Theme UI Helpers
 * All dialogs use Unicode (W) APIs and a dark color scheme
 * matching the OBS Studio appearance.
 * ================================================================ */

#ifdef _WIN32

#define MW_BG_COLOR RGB(26, 26, 46)
#define MW_BG_EDIT RGB(15, 52, 96)
#define MW_FG_COLOR RGB(224, 224, 224)
#define MW_FG_DIM RGB(136, 136, 136)
#define MW_ACCENT RGB(79, 195, 247)

static HBRUSH g_bg_brush = NULL;
static HBRUSH g_edit_brush = NULL;

static void ensure_brushes(void)
{
	if (!g_bg_brush)
		g_bg_brush = CreateSolidBrush(MW_BG_COLOR);
	if (!g_edit_brush)
		g_edit_brush = CreateSolidBrush(MW_BG_EDIT);
}

static HFONT create_font(int size, bool bold)
{
	return CreateFontW(-size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
			   L"Segoe UI");
}

/* Convert UTF-8 to wide string (caller must free result) */
static wchar_t *utf8_to_wide(const char *utf8)
{
	if (!utf8 || !utf8[0])
		return _wcsdup(L"");
	int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
	wchar_t *w = malloc(len * sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, len);
	return w;
}

/* Dark theme color handler — call from WM_CTLCOLORSTATIC / WM_CTLCOLOREDIT etc. */
static LRESULT handle_ctlcolor(HDC hdc, bool is_edit)
{
	SetTextColor(hdc, MW_FG_COLOR);
	SetBkColor(hdc, is_edit ? MW_BG_EDIT : MW_BG_COLOR);
	return (LRESULT)(is_edit ? g_edit_brush : g_bg_brush);
}

/* ================================================================
 * DSGVO Consent Dialog (Unicode, dark theme, clickable links)
 * ================================================================ */

#define IDC_CONSENT_YES 2001
#define IDC_CONSENT_NO 2002
#define IDC_CONSENT_DSGVO 2003

static bool g_consent_result = false;
static wchar_t g_consent_server_w[512];

static LRESULT CALLBACK consent_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE: {
		ensure_brushes();
		HFONT hFont = create_font(14, false);
		HFONT hFontBold = create_font(16, false);
		HFONT hTitleFont = create_font(18, true);
		HFONT hSmallFont = create_font(12, false);

		/* Measure actual line height for DPI-aware layout */
		HDC hdc = GetDC(hwnd);
		HFONT oldFont = (HFONT)SelectObject(hdc, hFont);
		TEXTMETRICW tm;
		GetTextMetricsW(hdc, &tm);
		int lh = tm.tmHeight + tm.tmExternalLeading; /* line height */
		SelectObject(hdc, oldFont);
		ReleaseDC(hwnd, hdc);

		int y = 15, x = 24, w = 432;

		/* Title */
		HWND title = CreateWindowW(L"STATIC", L"MW Aufnahme \u2013 Datenschutz (DSGVO)",
					   WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, lh + 8, hwnd, NULL, NULL, NULL);
		SendMessageW(title, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
		y += lh + 18;

		/* Info text: what data is transmitted (7 lines) */
		int h1 = lh * 7 + 4;
		HWND info1 = CreateWindowW(L"STATIC",
					   L"Durch die MW-Aufnahme werden folgende Daten\r\n"
					   L"an den Server \u00FCbermittelt:\r\n"
					   L"\r\n"
					   L"  \u2022  Dein Anzeigename\r\n"
					   L"  \u2022  Deine Kamera-ID (A\u2013H)\r\n"
					   L"  \u2022  Aufnahmestatus (online/offline)\r\n"
					   L"  \u2022  Zeitstempel (Start, Stop, Heartbeat)",
					   WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h1, hwnd, NULL, NULL, NULL);
		SendMessageW(info1, WM_SETFONT, (WPARAM)hFont, TRUE);
		y += h1 + 6;

		/* Privacy assurances (3 lines) */
		int h2 = lh * 3 + 4;
		HWND info2 = CreateWindowW(L"STATIC",
					   L"NUR der Regisseur kann diese Daten einsehen.\r\n"
					   L"Es werden KEINE Audio-, Video- oder Bilddaten\r\n"
					   L"\u00FCbertragen.",
					   WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h2, hwnd, NULL, NULL, NULL);
		SendMessageW(info2, WM_SETFONT, (WPARAM)hFont, TRUE);
		y += h2 + 6;

		/* Retention & revocation (3 lines) */
		int h3 = lh * 3 + 4;
		HWND info3 = CreateWindowW(L"STATIC",
					   L"Sessions werden nach 30 Tagen automatisch\r\n"
					   L"gel\u00F6scht. Du kannst deine Einwilligung\r\n"
					   L"jederzeit widerrufen.",
					   WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h3, hwnd, NULL, NULL, NULL);
		SendMessageW(info3, WM_SETFONT, (WPARAM)hFont, TRUE);
		y += h3 + 6;

		/* Server info */
		wchar_t server_buf[600];
		_snwprintf(server_buf, 600, L"Server: %ls", g_consent_server_w);
		HWND serverLabel = CreateWindowW(L"STATIC", server_buf,
						  WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, lh + 2, hwnd, NULL, NULL, NULL);
		SendMessageW(serverLabel, WM_SETFONT, (WPARAM)hSmallFont, TRUE);
		y += lh + 10;

		/* Separator line */
		CreateWindowW(L"STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, x, y, w, 2, hwnd, NULL, NULL,
			      NULL);
		y += 10;

		/* Datenschutzerklaerung button */
		HWND btnDsgvo = CreateWindowW(L"BUTTON", L"Datenschutzerkl\u00E4rung \u00F6ffnen",
					       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, 250, 32, hwnd,
					       (HMENU)(INT_PTR)IDC_CONSENT_DSGVO, NULL, NULL);
		SendMessageW(btnDsgvo, WM_SETFONT, (WPARAM)hFont, TRUE);
		y += 42;

		/* Question */
		HWND q = CreateWindowW(L"STATIC", L"Bist du damit einverstanden?",
				       WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, lh + 4, hwnd, NULL, NULL, NULL);
		SendMessageW(q, WM_SETFONT, (WPARAM)hFontBold, TRUE);
		y += lh + 14;

		/* Buttons */
		HWND btnYes = CreateWindowW(L"BUTTON", L"Ja, einverstanden",
					    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, x, y, 160, 38, hwnd,
					    (HMENU)(INT_PTR)IDC_CONSENT_YES, NULL, NULL);
		SendMessageW(btnYes, WM_SETFONT, (WPARAM)hFontBold, TRUE);

		HWND btnNo = CreateWindowW(L"BUTTON", L"Nein, ablehnen", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
					   x + 180, y, 160, 38, hwnd, (HMENU)(INT_PTR)IDC_CONSENT_NO, NULL, NULL);
		SendMessageW(btnNo, WM_SETFONT, (WPARAM)hFont, TRUE);

		/* Resize window to fit all content */
		y += 52; /* button height + bottom padding */
		RECT rc = {0, 0, 500, y};
		AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
		SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);

		return 0;
	}

	case WM_CTLCOLORSTATIC:
		return handle_ctlcolor((HDC)wParam, false);

	case WM_ERASEBKGND: {
		ensure_brushes();
		RECT rc;
		GetClientRect(hwnd, &rc);
		FillRect((HDC)wParam, &rc, g_bg_brush);
		return 1;
	}

	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_CONSENT_DSGVO) {
			wchar_t url[600];
			_snwprintf(url, 600, L"%ls/datenschutz.php", g_consent_server_w);
			ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
			return 0;
		}
		if (LOWORD(wParam) == IDC_CONSENT_YES) {
			g_consent_result = true;
			DestroyWindow(hwnd);
		} else if (LOWORD(wParam) == IDC_CONSENT_NO) {
			g_consent_result = false;
			DestroyWindow(hwnd);
		}
		return 0;

	case WM_CLOSE:
		g_consent_result = false;
		DestroyWindow(hwnd);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool show_consent_dialog(const char *server_url)
{
	wchar_t *server_w = utf8_to_wide(server_url);
	wcsncpy(g_consent_server_w, server_w, 511);
	g_consent_server_w[511] = 0;
	free(server_w);
	g_consent_result = false;

	static bool class_registered = false;
	if (!class_registered) {
		INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LINK_CLASS};
		InitCommonControlsEx(&icc);

		WNDCLASSW wc = {0};
		wc.lpfnWndProc = consent_wnd_proc;
		wc.hInstance = GetModuleHandleW(NULL);
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = NULL; /* We paint our own background */
		wc.lpszClassName = L"MWConsentDialog";
		RegisterClassW(&wc);
		class_registered = true;
	}

	/* Initial size is a placeholder; WM_CREATE resizes to fit content */
	HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"MWConsentDialog",
				    L"MW Aufnahme \u2013 DSGVO Einwilligung",
				    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 500,
				    600, NULL, NULL, GetModuleHandleW(NULL), NULL);

	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);

	MSG m;
	while (GetMessageW(&m, NULL, 0, 0)) {
		if (!IsWindow(hwnd))
			break;
		TranslateMessage(&m);
		DispatchMessageW(&m);
	}

	return g_consent_result;
}

/* ================================================================
 * Settings Dialog (Unicode, dark theme)
 * ================================================================ */

#define IDC_SERVER_URL 1001
#define IDC_USER_NAME 1002
#define IDC_API_KEY 1003
#define IDC_CAMERA_ID 1004
#define IDC_ENABLED 1005
#define IDC_SAVE 1006
#define IDC_RESET_CONSENT 1007
#define IDC_STATUS_LABEL 1008

static HWND g_settings_hwnd = NULL;

static void update_status_label(HWND hwnd)
{
	const wchar_t *text;
	if (g_mw.enabled) {
		if (g_mw.recording_active)
			text = L"\u25CF  Aufnahme l\u00E4uft \u2013 Daten werden gesendet";
		else if (g_mw.consent_given)
			text = L"\u25CF  Bereit \u2013 Warte auf Aufnahmestart";
		else
			text = L"\u25CB  Aktiviert \u2013 DSGVO-Einwilligung ausstehend";
	} else {
		text = L"\u25CB  Deaktiviert";
	}
	SetDlgItemTextW(hwnd, IDC_STATUS_LABEL, text);
}

static LRESULT CALLBACK settings_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE: {
		ensure_brushes();
		HFONT hFont = create_font(14, false);
		HFONT hFontBold = create_font(14, true);
		HFONT hTitleFont = create_font(18, true);
		HFONT hSmallFont = create_font(12, false);

		int y = 18;
		int lbl_x = 22, edit_x = 160, w = 290, h = 26;
		int full_w = 440;

		/* Title */
		HWND title = CreateWindowW(L"STATIC", L"MW Aufnahme", WS_CHILD | WS_VISIBLE | SS_LEFT, lbl_x, y,
					   full_w, 28, hwnd, NULL, NULL, NULL);
		SendMessageW(title, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
		y += 32;

		/* Status label */
		HWND status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, lbl_x, y, full_w, 20,
					    hwnd, (HMENU)(INT_PTR)IDC_STATUS_LABEL, NULL, NULL);
		SendMessageW(status, WM_SETFONT, (WPARAM)hSmallFont, TRUE);
		y += 30;

		/* Separator */
		CreateWindowW(L"STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, lbl_x, y, full_w, 2, hwnd,
			      NULL, NULL, NULL);
		y += 14;

		/* Enabled checkbox */
		HWND chk = CreateWindowW(L"BUTTON", L"MW Aufnahme aktivieren",
					 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, lbl_x, y, 300, h, hwnd,
					 (HMENU)(INT_PTR)IDC_ENABLED, NULL, NULL);
		SendMessageW(chk, WM_SETFONT, (WPARAM)hFontBold, TRUE);
		y += 34;

		/* Server URL */
		HWND lbl1 = CreateWindowW(L"STATIC", L"Server URL:", WS_CHILD | WS_VISIBLE | SS_LEFT, lbl_x, y + 3,
					  130, h, hwnd, NULL, NULL, NULL);
		SendMessageW(lbl1, WM_SETFONT, (WPARAM)hFont, TRUE);
		HWND ed1 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
					   edit_x, y, w, h, hwnd, (HMENU)(INT_PTR)IDC_SERVER_URL, NULL, NULL);
		SendMessageW(ed1, WM_SETFONT, (WPARAM)hFont, TRUE);
		y += 34;

		/* Anzeigename */
		HWND lbl2 = CreateWindowW(L"STATIC", L"Anzeigename:", WS_CHILD | WS_VISIBLE | SS_LEFT, lbl_x, y + 3,
					  130, h, hwnd, NULL, NULL, NULL);
		SendMessageW(lbl2, WM_SETFONT, (WPARAM)hFont, TRUE);
		HWND ed2 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
					   edit_x, y, w, h, hwnd, (HMENU)(INT_PTR)IDC_USER_NAME, NULL, NULL);
		SendMessageW(ed2, WM_SETFONT, (WPARAM)hFont, TRUE);
		y += 34;

		/* API Key */
		HWND lbl3 = CreateWindowW(L"STATIC", L"API Key:", WS_CHILD | WS_VISIBLE | SS_LEFT, lbl_x, y + 3, 130,
					  h, hwnd, NULL, NULL, NULL);
		SendMessageW(lbl3, WM_SETFONT, (WPARAM)hFont, TRUE);
		HWND ed3 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
					   WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD, edit_x, y, w, h,
					   hwnd, (HMENU)(INT_PTR)IDC_API_KEY, NULL, NULL);
		SendMessageW(ed3, WM_SETFONT, (WPARAM)hFont, TRUE);
		y += 34;

		/* Camera ID */
		HWND lbl4 = CreateWindowW(L"STATIC", L"Kamera:", WS_CHILD | WS_VISIBLE | SS_LEFT, lbl_x, y + 3, 130,
					  h, hwnd, NULL, NULL, NULL);
		SendMessageW(lbl4, WM_SETFONT, (WPARAM)hFont, TRUE);
		HWND combo = CreateWindowW(L"COMBOBOX", L"",
					   WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, edit_x, y, 80,
					   200, hwnd, (HMENU)(INT_PTR)IDC_CAMERA_ID, NULL, NULL);
		SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
		for (int i = 0; i < 8; i++) {
			wchar_t cam[4];
			cam[0] = L'A' + i;
			cam[1] = 0;
			SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)cam);
		}
		y += 44;

		/* Separator */
		CreateWindowW(L"STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, lbl_x, y, full_w, 2, hwnd,
			      NULL, NULL, NULL);
		y += 14;

		/* Buttons */
		HWND btn = CreateWindowW(L"BUTTON", L"Speichern", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, lbl_x, y,
					 130, 34, hwnd, (HMENU)(INT_PTR)IDC_SAVE, NULL, NULL);
		SendMessageW(btn, WM_SETFONT, (WPARAM)hFontBold, TRUE);

		HWND btn2 = CreateWindowW(L"BUTTON", L"DSGVO zur\u00FCcksetzen",
					  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, lbl_x + 145, y, 170, 34, hwnd,
					  (HMENU)(INT_PTR)IDC_RESET_CONSENT, NULL, NULL);
		SendMessageW(btn2, WM_SETFONT, (WPARAM)hFont, TRUE);

		/* Populate fields from config */
		pthread_mutex_lock(&g_mw.mutex);
		{
			wchar_t *wu = utf8_to_wide(g_mw.server_url);
			SetDlgItemTextW(hwnd, IDC_SERVER_URL, wu);
			free(wu);
		}
		{
			wchar_t *wu = utf8_to_wide(g_mw.user_name);
			SetDlgItemTextW(hwnd, IDC_USER_NAME, wu);
			free(wu);
		}
		{
			wchar_t *wu = utf8_to_wide(g_mw.api_key);
			SetDlgItemTextW(hwnd, IDC_API_KEY, wu);
			free(wu);
		}
		SendDlgItemMessageW(hwnd, IDC_CAMERA_ID, CB_SETCURSEL, g_mw.camera_id, 0);
		CheckDlgButton(hwnd, IDC_ENABLED, g_mw.enabled ? BST_CHECKED : BST_UNCHECKED);
		pthread_mutex_unlock(&g_mw.mutex);

		update_status_label(hwnd);
		return 0;
	}

	case WM_CTLCOLORSTATIC:
		return handle_ctlcolor((HDC)wParam, false);

	case WM_CTLCOLOREDIT:
		return handle_ctlcolor((HDC)wParam, true);

	case WM_CTLCOLORLISTBOX:
		return handle_ctlcolor((HDC)wParam, true);

	case WM_ERASEBKGND: {
		ensure_brushes();
		RECT rc;
		GetClientRect(hwnd, &rc);
		FillRect((HDC)wParam, &rc, g_bg_brush);
		return 1;
	}

	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_SAVE) {
			wchar_t wbuf[512];

			pthread_mutex_lock(&g_mw.mutex);

			GetDlgItemTextW(hwnd, IDC_SERVER_URL, wbuf, 512);
			WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, g_mw.server_url, sizeof(g_mw.server_url), NULL,
					    NULL);

			GetDlgItemTextW(hwnd, IDC_USER_NAME, wbuf, 100);
			WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, g_mw.user_name, sizeof(g_mw.user_name), NULL,
					    NULL);

			GetDlgItemTextW(hwnd, IDC_API_KEY, wbuf, 256);
			WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, g_mw.api_key, sizeof(g_mw.api_key), NULL, NULL);

			g_mw.camera_id = (int)SendDlgItemMessageW(hwnd, IDC_CAMERA_ID, CB_GETCURSEL, 0, 0);
			if (g_mw.camera_id < 0)
				g_mw.camera_id = 0;
			g_mw.enabled = IsDlgButtonChecked(hwnd, IDC_ENABLED) == BST_CHECKED;

			pthread_mutex_unlock(&g_mw.mutex);

			save_config();
			update_status_label(hwnd);
			obs_log(LOG_INFO, "MW recording settings saved (enabled=%d)", g_mw.enabled);
			MessageBoxW(hwnd, L"Einstellungen gespeichert!", L"MW Aufnahme", MB_OK | MB_ICONINFORMATION);
		}

		if (LOWORD(wParam) == IDC_RESET_CONSENT) {
			int r = MessageBoxW(hwnd,
					    L"DSGVO-Einwilligung zur\u00FCcksetzen?\n\n"
					    L"Beim n\u00E4chsten Aufnahmestart wirst du "
					    L"erneut nach deiner Einwilligung gefragt.",
					    L"MW Aufnahme", MB_YESNO | MB_ICONQUESTION);
			if (r == IDYES) {
				g_mw.consent_given = false;
				save_config();
				update_status_label(hwnd);
				obs_log(LOG_INFO, "MW recording: consent reset");
				MessageBoxW(hwnd, L"Einwilligung zur\u00FCckgesetzt.", L"MW Aufnahme",
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

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void open_settings_dialog(void)
{
	if (g_settings_hwnd) {
		SetForegroundWindow(g_settings_hwnd);
		return;
	}

	static bool class_registered = false;
	if (!class_registered) {
		WNDCLASSW wc = {0};
		wc.lpfnWndProc = settings_wnd_proc;
		wc.hInstance = GetModuleHandleW(NULL);
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = NULL;
		wc.lpszClassName = L"MWRecordingSettings";
		RegisterClassW(&wc);
		class_registered = true;
	}

	g_settings_hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MWRecordingSettings", L"MW Aufnahme",
					  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
					  500, 420, NULL, NULL, GetModuleHandleW(NULL), NULL);

	ShowWindow(g_settings_hwnd, SW_SHOW);
	UpdateWindow(g_settings_hwnd);
}

#endif /* _WIN32 */

/* ---- Consent handling ---- */

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
			MessageBoxW(NULL,
				    L"Achtung: Die Aufnahme wurde pausiert!\n\n"
				    L"Das Pausieren der Aufnahme kann den "
				    L"Timecode-Sync zerst\u00F6ren.\n\n"
				    L"Bitte die Aufnahme nicht pausieren, "
				    L"sondern stoppen und neu starten.",
				    L"MW Aufnahme \u2013 Warnung", MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL);
		}
#endif
	}
}

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
	if (g_bg_brush)
		DeleteObject(g_bg_brush);
	if (g_edit_brush)
		DeleteObject(g_edit_brush);
	g_bg_brush = NULL;
	g_edit_brush = NULL;
#endif

	pthread_mutex_destroy(&g_mw.mutex);
}

#endif /* ENABLE_FRONTEND_API */
