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
 * Adds a "MW Aufnahme" entry under OBS Tools menu.
 * Hooks into recording start/stop to send status to a remote server.
 * Settings are stored in OBS user config (global, not per-source).
 */

#pragma once

#ifdef ENABLE_FRONTEND_API

/*
 * Initialize MW recording: register Tools menu item + frontend event callback.
 * Call once from obs_module_load().
 */
void mw_recording_init(void);

/*
 * Cleanup MW recording state.
 * Call once from obs_module_unload().
 */
void mw_recording_cleanup(void);

#endif /* ENABLE_FRONTEND_API */
