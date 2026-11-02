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
 * auto-setup.c - First-run auto-setup for Minewache-New scene collection
 *
 * On first OBS launch after plugin install, detects if the Minewache-New
 * scene collection is available and offers to switch to it.
 * Also detects legacy "Minewache" and offers upgrade to Minewache-New.
 * The Minewache-New template has LTC Timecode pre-configured on Track 3.
 */

#ifdef ENABLE_FRONTEND_API

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <util/config-file.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "auto-setup.h"

#define CONFIG_SECTION "obs-ltc-timecode"
#define CONFIG_KEY_SETUP_DONE "auto_setup_done"
#define SCENE_COLLECTION_NAME "Minewache-New"
#define PROFILE_NAME "Minewache-New"
#define LEGACY_SCENE_COLLECTION_NAME "Minewache"
#define LEGACY_PROFILE_NAME "Minewache"

/* TICKET-082: fixed UUIDs of the shipped kit. Identifying by UUID instead of
 * by name keeps the migration off any self-built collection. */
#define CONFIG_KEY_KIT_GAME_AUDIO_DONE "kit_game_audio_done"
#define KIT_GAME_CAPTURE_UUID "b4d23cea-069d-412e-bf6c-84c495ae6c10"
#define KIT_GAME_SOUNDS_UUID "3506fe35-9121-4192-a84b-feba658d54e7"
#define KIT_LTC_TRACK_MIXER (1u << 2)

static bool has_scene_collection(const char *name)
{
	char **collections = obs_frontend_get_scene_collections();
	if (!collections)
		return false;

	bool found = false;
	for (char **c = collections; *c; c++) {
		if (strcmp(*c, name) == 0) {
			found = true;
			break;
		}
	}
	bfree(collections);
	return found;
}

static bool has_profile(const char *name)
{
	char **profiles = obs_frontend_get_profiles();
	if (!profiles)
		return false;

	bool found = false;
	for (char **p = profiles; *p; p++) {
		if (strcmp(*p, name) == 0) {
			found = true;
			break;
		}
	}
	bfree(profiles);
	return found;
}

static bool is_current_scene_collection(const char *name)
{
	char *current = obs_frontend_get_current_scene_collection();
	if (!current)
		return false;

	bool match = strcmp(current, name) == 0;
	bfree(current);
	return match;
}

static bool show_switch_dialog(void)
{
#ifdef _WIN32
	/* MessageBoxW, not ...A: the source file is UTF-8, and the ANSI API
	 * renders the umlauts as mojibake ("MÃ¶chtest") on a German Windows.
	 * Non-technical users screenshot exactly such dialogs into Discord —
	 * a garbled dialog destroys trust in the whole setup. Same migration
	 * the MW dialogs got in TICKET-030. */
	int result = MessageBoxW(NULL,
				 L"Das Minewache-New Template mit vorkonfiguriertem "
				 L"LTC Timecode (Track 3) wurde erkannt.\n\n"
				 L"M\u00F6chtest du zur Minewache-New Scene Collection "
				 L"wechseln?\n\n"
				 L"Track 1: Stimmen Audio\n"
				 L"Track 2: Ingame Audio\n"
				 L"Track 3: LTC Timecode (f\u00FCr DaVinci Resolve Sync)",
				 L"OBS LTC Timecode - Setup", MB_YESNO | MB_ICONQUESTION | MB_SYSTEMMODAL);
	return result == IDYES;
#else
	/* On Linux, auto-switch without dialog */
	obs_log(LOG_INFO, "Minewache-New scene collection found, switching automatically");
	return true;
#endif
}

static bool show_upgrade_dialog(void)
{
#ifdef _WIN32
	int result = MessageBoxW(NULL,
				 L"Ein \u00E4lteres Minewache-Template wurde erkannt.\n\n"
				 L"M\u00F6chtest du auf das neue Minewache-New Template "
				 L"upgraden?\n\n"
				 L"Das neue Template hat LTC Timecode vorkonfiguriert "
				 L"auf Track 3.",
				 L"OBS LTC Timecode - Upgrade", MB_YESNO | MB_ICONQUESTION | MB_SYSTEMMODAL);
	return result == IDYES;
#else
	obs_log(LOG_INFO, "Legacy Minewache found, upgrading to Minewache-New");
	return true;
#endif
}

static void mark_setup_done(void)
{
	config_t *config = obs_frontend_get_user_config();
	if (!config)
		return;

	config_set_bool(config, CONFIG_SECTION, CONFIG_KEY_SETUP_DONE, true);
	config_save(config);
}

#ifdef _WIN32

static bool show_kit_game_audio_dialog(void)
{
	int result = MessageBoxW(NULL,
				 L"Update für das Minewache OBS-Kit:\n\n"
				 L"Der Spielton soll künftig direkt von der Spielaufnahme "
				 L"kommen. Dann gibt es ohne Bild auch keinen Ingame-Ton mehr, "
				 L"und der Fehler fällt sofort auf.\n\n"
				 L"\"Minewache Sounds\" wird dafür ausgeblendet, deine "
				 L"Lautstärke bleibt erhalten.\n\n"
				 L"Jetzt umstellen?",
				 L"OBS LTC Timecode - Kit-Update", MB_YESNO | MB_ICONQUESTION | MB_SYSTEMMODAL);
	return result == IDYES;
}

static bool hide_source_in_scene(void *param, obs_source_t *scene_source)
{
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	obs_sceneitem_t *item = scene ? obs_scene_find_source(scene, param) : NULL;

	if (item)
		obs_sceneitem_set_visible(item, false);
	return true;
}

static void apply_kit_game_audio(obs_source_t *game, obs_source_t *sounds)
{
	/* Mixers first: the kit's game capture also sits on the LTC track, and
	 * game sound must never reach it. */
	obs_source_set_audio_mixers(game, obs_source_get_audio_mixers(sounds) & ~KIT_LTC_TRACK_MIXER);
	obs_source_set_volume(game, obs_source_get_volume(sounds));
	obs_source_set_muted(game, obs_source_muted(sounds));
	obs_source_set_sync_offset(game, obs_source_get_sync_offset(sounds));

	obs_data_t *settings = obs_data_create();
	obs_data_set_bool(settings, "capture_audio", true);
	obs_source_update(game, settings);
	obs_data_release(settings);

	/* Hidden, not removed: showing it again is the way back. */
	obs_enum_scenes(hide_source_in_scene, (void *)obs_source_get_name(sounds));

	obs_frontend_save();
	obs_log(LOG_INFO, "kit update: game audio now comes from the game capture");
}

static void migrate_kit_game_audio(void *param)
{
	static bool running = false;
	(void)param;

	/* The dialog pumps messages, a second queued task could run inside it */
	if (running || !is_current_scene_collection(SCENE_COLLECTION_NAME) || obs_frontend_recording_active())
		return;
	running = true;

	obs_source_t *game = obs_get_source_by_uuid(KIT_GAME_CAPTURE_UUID);
	if (!game) {
		running = false;
		return;
	}

	obs_data_t *settings = obs_source_get_settings(game);
	bool capture_audio = obs_data_get_bool(settings, "capture_audio");
	obs_data_release(settings);

	config_t *config = obs_frontend_get_user_config();
	bool done = config && config_get_bool(config, CONFIG_SECTION, CONFIG_KEY_KIT_GAME_AUDIO_DONE);

	if (capture_audio) {
		/* Enabled by hand: runs on every start, protects the LTC track. */
		uint32_t mixers = obs_source_get_audio_mixers(game);
		if (mixers & KIT_LTC_TRACK_MIXER) {
			obs_source_set_audio_mixers(game, mixers & ~KIT_LTC_TRACK_MIXER);
			obs_frontend_save();
			obs_log(LOG_WARNING, "kit update: removed game audio from the LTC track");
		}
		done = true;
	} else if (!done) {
		obs_source_t *sounds = obs_get_source_by_uuid(KIT_GAME_SOUNDS_UUID);
		if (sounds) {
			if (show_kit_game_audio_dialog())
				apply_kit_game_audio(game, sounds);
			else
				obs_log(LOG_INFO, "kit update: declined by user");
			obs_source_release(sounds);
			done = true;
		}
	}

	if (done && config) {
		config_set_bool(config, CONFIG_SECTION, CONFIG_KEY_KIT_GAME_AUDIO_DONE, true);
		config_save(config);
	}
	obs_source_release(game);
	running = false;
}

#endif /* _WIN32 */

static void run_auto_setup(void)
{
	/* Check if auto-setup was already performed */
	config_t *config = obs_frontend_get_user_config();
	if (config && config_get_bool(config, CONFIG_SECTION, CONFIG_KEY_SETUP_DONE)) {
		obs_log(LOG_DEBUG, "auto-setup already done, skipping");
		return;
	}

	/* Check if legacy Minewache is active and offer upgrade */
	if (has_scene_collection(LEGACY_SCENE_COLLECTION_NAME) && has_scene_collection(SCENE_COLLECTION_NAME)) {
		if (is_current_scene_collection(LEGACY_SCENE_COLLECTION_NAME)) {
			if (show_upgrade_dialog()) {
				obs_log(LOG_INFO, "upgrading from Minewache to "
						  "Minewache-New...");
				obs_frontend_set_current_scene_collection(SCENE_COLLECTION_NAME);
				if (has_profile(PROFILE_NAME)) {
					obs_frontend_set_current_profile(PROFILE_NAME);
				}
				obs_log(LOG_INFO, "upgrade complete: Minewache-New "
						  "active (LTC Timecode on Track 3)");
			} else {
				obs_log(LOG_INFO, "user declined upgrade to "
						  "Minewache-New");
			}
			mark_setup_done();
			return;
		}
	}

	/* Check if Minewache-New scene collection is installed */
	if (!has_scene_collection(SCENE_COLLECTION_NAME)) {
		obs_log(LOG_INFO, "Minewache-New scene collection not found, "
				  "skipping auto-setup");
		return;
	}

	/* Already active? Just mark done */
	if (is_current_scene_collection(SCENE_COLLECTION_NAME)) {
		obs_log(LOG_INFO, "Minewache-New scene collection already active");
		mark_setup_done();
		return;
	}

	/* Ask user if they want to switch */
	if (show_switch_dialog()) {
		obs_log(LOG_INFO, "switching to Minewache-New scene collection...");
		obs_frontend_set_current_scene_collection(SCENE_COLLECTION_NAME);

		if (has_profile(PROFILE_NAME)) {
			obs_log(LOG_INFO, "switching to Minewache-New profile...");
			obs_frontend_set_current_profile(PROFILE_NAME);
		}

		obs_log(LOG_INFO, "auto-setup complete: Minewache-New template "
				  "active (LTC Timecode on Track 3)");
	} else {
		obs_log(LOG_INFO, "user declined Minewache-New template switch");
	}

	mark_setup_done();
}

static void on_frontend_event(enum obs_frontend_event event, void *data)
{
	(void)data;

	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		run_auto_setup();

	/* OBS may start in another collection and switch later, so the startup
	 * check alone misses the kit. Queued: not from inside the frontend
	 * callback that is still activating the collection. */
#ifdef _WIN32
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING || event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED)
		obs_queue_task(OBS_TASK_UI, migrate_kit_game_audio, NULL, false);
#endif
}

void auto_setup_init(void)
{
	obs_frontend_add_event_callback(on_frontend_event, NULL);
	obs_log(LOG_INFO, "auto-setup initialized");
}

void auto_setup_cleanup(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, NULL);
}

#endif /* ENABLE_FRONTEND_API */
