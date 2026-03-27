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
 * Sends recording start/stop/heartbeat signals to a remote server.
 * Uses libcurl for HTTP POST requests in a background thread.
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
#endif

#define MW_HEARTBEAT_INTERVAL_SEC 30
#define MW_CONFIG_SECTION "mw-recording"
#define MW_CONFIG_CONSENT "consent_given"

struct mw_recording {
	/* Settings (protected by mutex for thread safety) */
	pthread_mutex_t mutex;
	char server_url[512];
	char user_name[100];
	char api_key[256];
	int camera_id;
	bool enabled;

	/* Runtime state */
	bool recording_active;
	bool consent_given;

	/* Heartbeat thread */
	pthread_t heartbeat_thread;
	os_event_t *stop_event;
	bool thread_created;
};

/* Global instance pointer (set during recording) */
static mw_recording_t *g_mw_instance = NULL;

/* ---- curl helpers ---- */

/* Discard response body */
static size_t discard_write(char *ptr, size_t size, size_t nmemb, void *data)
{
	(void)ptr;
	(void)data;
	return size * nmemb;
}

/*
 * Send a POST request to the MW server API.
 * url_suffix: e.g. "?action=start"
 * json_body: JSON string to send as POST body
 */
static bool mw_http_post(const char *base_url, const char *url_suffix,
			  const char *api_key, const char *json_body)
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
		obs_log(LOG_WARNING, "MW HTTP POST failed: %s (url: %s)",
			curl_easy_strerror(res), url);
		return false;
	}

	return true;
}

/* ---- Heartbeat thread ---- */

static void *heartbeat_thread_func(void *data)
{
	mw_recording_t *mw = data;

	os_set_thread_name("mw-heartbeat");

	while (os_event_timedwait(mw->stop_event, 0) != 0) {
		char server[512];
		char name[100];
		char key[256];

		pthread_mutex_lock(&mw->mutex);
		snprintf(server, sizeof(server), "%s", mw->server_url);
		snprintf(name, sizeof(name), "%s", mw->user_name);
		snprintf(key, sizeof(key), "%s", mw->api_key);
		bool active = mw->recording_active;
		pthread_mutex_unlock(&mw->mutex);

		if (active && server[0] && name[0]) {
			char body[256];
			snprintf(body, sizeof(body),
				 "{\"name\":\"%s\"}", name);
			mw_http_post(server, "?action=heartbeat", key, body);
			obs_log(LOG_DEBUG, "MW heartbeat sent for '%s'", name);
		}

		if (os_event_timedwait(mw->stop_event,
				       MW_HEARTBEAT_INTERVAL_SEC * 1000) == 0)
			return NULL;
	}

	return NULL;
}

static void start_heartbeat_thread(mw_recording_t *mw)
{
	if (mw->thread_created)
		return;

	if (os_event_init(&mw->stop_event, OS_EVENT_TYPE_MANUAL) != 0)
		return;

	if (pthread_create(&mw->heartbeat_thread, NULL,
			   heartbeat_thread_func, mw) == 0) {
		mw->thread_created = true;
	} else {
		os_event_destroy(mw->stop_event);
		mw->stop_event = NULL;
	}
}

static void stop_heartbeat_thread(mw_recording_t *mw)
{
	if (!mw->thread_created)
		return;

	os_event_signal(mw->stop_event);
	pthread_join(mw->heartbeat_thread, NULL);
	os_event_destroy(mw->stop_event);
	mw->stop_event = NULL;
	mw->thread_created = false;
}

/* ---- Consent handling ---- */

static bool check_consent(void)
{
	config_t *config = obs_frontend_get_user_config();
	if (!config)
		return false;
	return config_get_bool(config, MW_CONFIG_SECTION, MW_CONFIG_CONSENT);
}

static void save_consent(bool consent)
{
	config_t *config = obs_frontend_get_user_config();
	if (!config)
		return;
	config_set_bool(config, MW_CONFIG_SECTION, MW_CONFIG_CONSENT, consent);
	config_save(config);
}

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

	int result = MessageBoxA(NULL, msg,
				 "MW Aufnahme - DSGVO Einwilligung",
				 MB_YESNO | MB_ICONQUESTION | MB_SYSTEMMODAL);
	return result == IDYES;
}
#endif

static bool ensure_consent(mw_recording_t *mw)
{
	if (mw->consent_given)
		return true;

	if (check_consent()) {
		mw->consent_given = true;
		return true;
	}

#ifdef _WIN32
	char server[512];
	char name[100];
	char key[256];

	pthread_mutex_lock(&mw->mutex);
	snprintf(server, sizeof(server), "%s", mw->server_url);
	snprintf(name, sizeof(name), "%s", mw->user_name);
	snprintf(key, sizeof(key), "%s", mw->api_key);
	pthread_mutex_unlock(&mw->mutex);

	if (show_consent_dialog(server)) {
		mw->consent_given = true;
		save_consent(true);

		/* Log consent on server */
		char body[512];
		snprintf(body, sizeof(body),
			 "{\"name\":\"%s\",\"consent\":true}", name);
		mw_http_post(server, "?action=consent", key, body);

		obs_log(LOG_INFO, "MW recording: user '%s' gave consent",
			name);
		return true;
	}

	obs_log(LOG_INFO, "MW recording: user declined consent");
	return false;
#else
	obs_log(LOG_WARNING,
		"MW recording consent dialog not available on this platform");
	return false;
#endif
}

/* ---- Recording start/stop ---- */

static void mw_send_start(mw_recording_t *mw)
{
	char server[512];
	char name[100];
	char key[256];
	int cam;

	pthread_mutex_lock(&mw->mutex);
	snprintf(server, sizeof(server), "%s", mw->server_url);
	snprintf(name, sizeof(name), "%s", mw->user_name);
	snprintf(key, sizeof(key), "%s", mw->api_key);
	cam = mw->camera_id;
	pthread_mutex_unlock(&mw->mutex);

	if (!server[0] || !name[0]) {
		obs_log(LOG_WARNING,
			"MW recording: server URL or name not configured");
		return;
	}

	char body[512];
	snprintf(body, sizeof(body),
		 "{\"name\":\"%s\",\"camera_id\":\"%c\"}",
		 name, 'A' + cam);
	mw_http_post(server, "?action=start", key, body);

	obs_log(LOG_INFO, "MW recording started: '%s' camera %c",
		name, 'A' + cam);
}

static void mw_send_stop(mw_recording_t *mw)
{
	char server[512];
	char name[100];
	char key[256];
	int cam;

	pthread_mutex_lock(&mw->mutex);
	snprintf(server, sizeof(server), "%s", mw->server_url);
	snprintf(name, sizeof(name), "%s", mw->user_name);
	snprintf(key, sizeof(key), "%s", mw->api_key);
	cam = mw->camera_id;
	pthread_mutex_unlock(&mw->mutex);

	if (!server[0] || !name[0])
		return;

	char body[512];
	snprintf(body, sizeof(body),
		 "{\"name\":\"%s\",\"camera_id\":\"%c\"}",
		 name, 'A' + cam);
	mw_http_post(server, "?action=stop", key, body);

	obs_log(LOG_INFO, "MW recording stopped: '%s' camera %c",
		name, 'A' + cam);
}

/* ---- Frontend event callback ---- */

static void on_frontend_event(enum obs_frontend_event event, void *data)
{
	(void)data;

	mw_recording_t *mw = g_mw_instance;
	if (!mw || !mw->enabled)
		return;

	if (event == OBS_FRONTEND_EVENT_RECORDING_STARTING) {
		/* Check consent before recording actually starts */
		if (!ensure_consent(mw)) {
			/* User declined consent — we cannot block the
			 * recording from here, but we skip sending data. */
			obs_log(LOG_INFO,
				"MW recording: skipping (no consent)");
			return;
		}
	}

	if (event == OBS_FRONTEND_EVENT_RECORDING_STARTED) {
		if (!mw->consent_given)
			return;

		pthread_mutex_lock(&mw->mutex);
		mw->recording_active = true;
		pthread_mutex_unlock(&mw->mutex);

		mw_send_start(mw);
		start_heartbeat_thread(mw);
	}

	if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
		pthread_mutex_lock(&mw->mutex);
		bool was_active = mw->recording_active;
		mw->recording_active = false;
		pthread_mutex_unlock(&mw->mutex);

		if (was_active) {
			stop_heartbeat_thread(mw);
			mw_send_stop(mw);
		}
	}

	if (event == OBS_FRONTEND_EVENT_RECORDING_PAUSED) {
		/* Warn user that pausing breaks timecode sync */
#ifdef _WIN32
		if (mw->enabled && mw->recording_active) {
			MessageBoxA(
				NULL,
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

/* ---- Public API ---- */

mw_recording_t *mw_recording_create(void)
{
	mw_recording_t *mw = bzalloc(sizeof(mw_recording_t));
	pthread_mutex_init(&mw->mutex, NULL);
	mw->consent_given = check_consent();

	g_mw_instance = mw;

	obs_log(LOG_INFO, "MW recording context created");
	return mw;
}

void mw_recording_destroy(mw_recording_t *mw)
{
	if (!mw)
		return;

	/* Stop heartbeat if running */
	stop_heartbeat_thread(mw);

	/* Send stop if still active */
	if (mw->recording_active) {
		mw->recording_active = false;
		mw_send_stop(mw);
	}

	if (g_mw_instance == mw)
		g_mw_instance = NULL;

	pthread_mutex_destroy(&mw->mutex);
	bfree(mw);

	obs_log(LOG_INFO, "MW recording context destroyed");
}

void mw_recording_update(mw_recording_t *mw, const char *server_url,
			  const char *user_name, const char *api_key,
			  int camera_id, bool enabled)
{
	if (!mw)
		return;

	pthread_mutex_lock(&mw->mutex);

	if (server_url)
		snprintf(mw->server_url, sizeof(mw->server_url), "%s",
			 server_url);
	if (user_name)
		snprintf(mw->user_name, sizeof(mw->user_name), "%s",
			 user_name);
	if (api_key)
		snprintf(mw->api_key, sizeof(mw->api_key), "%s", api_key);
	mw->camera_id = camera_id;
	mw->enabled = enabled;

	pthread_mutex_unlock(&mw->mutex);
}

void mw_recording_init(void)
{
	obs_frontend_add_event_callback(on_frontend_event, NULL);
	obs_log(LOG_INFO, "MW recording initialized");
}

void mw_recording_cleanup(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, NULL);
}

#endif /* ENABLE_FRONTEND_API */
