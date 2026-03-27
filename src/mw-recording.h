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
 * mw-recording.h - MW recording status reporting
 *
 * Sends recording start/stop/heartbeat signals to a remote PHP server
 * so a central dashboard can show which cameras are currently recording.
 * Includes DSGVO consent handling before any data is transmitted.
 */

#pragma once

#ifdef ENABLE_FRONTEND_API

#include <stdbool.h>

typedef struct mw_recording mw_recording_t;

/*
 * Create a new MW recording context. Call once per LTC source instance.
 */
mw_recording_t *mw_recording_create(void);

/*
 * Destroy the MW recording context and stop any background threads.
 */
void mw_recording_destroy(mw_recording_t *mw);

/*
 * Update settings from OBS source properties.
 * Called when the user changes settings in the source properties dialog.
 */
void mw_recording_update(mw_recording_t *mw, const char *server_url,
			  const char *user_name, const char *api_key,
			  int camera_id, bool enabled);

/*
 * Initialize global MW recording state (frontend event callbacks).
 * Call once from obs_module_load().
 */
void mw_recording_init(void);

/*
 * Cleanup global MW recording state.
 * Call once from obs_module_unload().
 */
void mw_recording_cleanup(void);

/* OBS source property keys for MW recording */
#define S_MW_ENABLED "mw_enabled"
#define S_MW_SERVER_URL "mw_server_url"
#define S_MW_USER_NAME "mw_user_name"
#define S_MW_API_KEY "mw_api_key"

#endif /* ENABLE_FRONTEND_API */
