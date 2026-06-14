/*
 * obs-ltc-timecode - NTP-synced LTC timecode audio source for OBS Studio
 * Copyright (C) 2024 Ferdmusic
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
 */

#include <obs-module.h>
#include <plugin-support.h>

#include "ltc-source.h"
#include "ntp-client.h"
#include "http-time-client.h"
#ifdef ENABLE_FRONTEND_API
#include "auto-setup.h"
#include "mw-recording.h"
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

const char *obs_module_description(void)
{
	return "NTP-synchronized LTC timecode audio source for multi-camera sync";
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "loading plugin (version %s)...", PLUGIN_VERSION);

	if (!ntp_platform_init()) {
		obs_log(LOG_WARNING, "NTP platform init failed (network features may not work)");
	}

	if (!http_time_init()) {
		obs_log(LOG_WARNING, "HTTP time fallback init failed");
	}

	ltc_source_register();

#ifdef ENABLE_FRONTEND_API
	auto_setup_init();
	mw_recording_init();
#endif

	obs_log(LOG_INFO,
		"plugin loaded successfully (version %s) — "
		"source 'LTC Timecode Generator' registered",
		PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
#ifdef ENABLE_FRONTEND_API
	mw_recording_cleanup();
	auto_setup_cleanup();
#endif
	http_time_cleanup();
	ntp_platform_cleanup();
	obs_log(LOG_INFO, "plugin unloaded");
}
