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
	int result = MessageBoxA(
		NULL,
		"Das Minewache-New Template mit vorkonfiguriertem "
		"LTC Timecode (Track 3) wurde erkannt.\n\n"
		"Moechtest du zur Minewache-New Scene Collection wechseln?\n\n"
		"Track 1: Stimmen Audio\n"
		"Track 2: Ingame Audio\n"
		"Track 3: LTC Timecode (fuer DaVinci Resolve Sync)",
		"OBS LTC Timecode - Setup",
		MB_YESNO | MB_ICONQUESTION | MB_SYSTEMMODAL);
	return result == IDYES;
#else
	/* On Linux, auto-switch without dialog */
	obs_log(LOG_INFO,
		"Minewache-New scene collection found, switching automatically");
	return true;
#endif
}

static bool show_upgrade_dialog(void)
{
#ifdef _WIN32
	int result = MessageBoxA(
		NULL,
		"Ein aelteres Minewache-Template wurde erkannt.\n\n"
		"Moechtest du auf das neue Minewache-New Template "
		"upgraden?\n\n"
		"Das neue Template hat LTC Timecode vorkonfiguriert "
		"auf Track 3.",
		"OBS LTC Timecode - Upgrade",
		MB_YESNO | MB_ICONQUESTION | MB_SYSTEMMODAL);
	return result == IDYES;
#else
	obs_log(LOG_INFO,
		"Legacy Minewache found, upgrading to Minewache-New");
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

static void on_frontend_event(enum obs_frontend_event event, void *data)
{
	(void)data;

	if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING)
		return;

	/* Check if auto-setup was already performed */
	config_t *config = obs_frontend_get_user_config();
	if (config &&
	    config_get_bool(config, CONFIG_SECTION, CONFIG_KEY_SETUP_DONE)) {
		obs_log(LOG_DEBUG, "auto-setup already done, skipping");
		return;
	}

	/* Check if legacy Minewache is active and offer upgrade */
	if (has_scene_collection(LEGACY_SCENE_COLLECTION_NAME) &&
	    has_scene_collection(SCENE_COLLECTION_NAME)) {
		if (is_current_scene_collection(LEGACY_SCENE_COLLECTION_NAME)) {
			if (show_upgrade_dialog()) {
				obs_log(LOG_INFO,
					"upgrading from Minewache to "
					"Minewache-New...");
				obs_frontend_set_current_scene_collection(
					SCENE_COLLECTION_NAME);
				if (has_profile(PROFILE_NAME)) {
					obs_frontend_set_current_profile(
						PROFILE_NAME);
				}
				obs_log(LOG_INFO,
					"upgrade complete: Minewache-New "
					"active (LTC Timecode on Track 3)");
			} else {
				obs_log(LOG_INFO,
					"user declined upgrade to "
					"Minewache-New");
			}
			mark_setup_done();
			return;
		}
	}

	/* Check if Minewache-New scene collection is installed */
	if (!has_scene_collection(SCENE_COLLECTION_NAME)) {
		obs_log(LOG_INFO,
			"Minewache-New scene collection not found, "
			"skipping auto-setup");
		return;
	}

	/* Already active? Just mark done */
	if (is_current_scene_collection(SCENE_COLLECTION_NAME)) {
		obs_log(LOG_INFO,
			"Minewache-New scene collection already active");
		mark_setup_done();
		return;
	}

	/* Ask user if they want to switch */
	if (show_switch_dialog()) {
		obs_log(LOG_INFO,
			"switching to Minewache-New scene collection...");
		obs_frontend_set_current_scene_collection(
			SCENE_COLLECTION_NAME);

		if (has_profile(PROFILE_NAME)) {
			obs_log(LOG_INFO,
				"switching to Minewache-New profile...");
			obs_frontend_set_current_profile(PROFILE_NAME);
		}

		obs_log(LOG_INFO,
			"auto-setup complete: Minewache-New template "
			"active (LTC Timecode on Track 3)");
	} else {
		obs_log(LOG_INFO,
			"user declined Minewache-New template switch");
	}

	mark_setup_done();
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
