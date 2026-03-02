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
 * auto-setup.h - First-run auto-setup for Minewache-New scene collection
 *
 * On first OBS launch after plugin install, offers to switch to the
 * pre-configured Minewache-New scene collection with LTC on Track 3.
 * Also detects legacy "Minewache" and offers upgrade to Minewache-New.
 */

#pragma once

#ifdef ENABLE_FRONTEND_API
void auto_setup_init(void);
void auto_setup_cleanup(void);
#endif
