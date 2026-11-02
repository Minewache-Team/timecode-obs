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
 * audio-monitor.h - Warns when a source delivers no sound while recording
 */

#ifndef AUDIO_MONITOR_H
#define AUDIO_MONITOR_H

#ifdef ENABLE_FRONTEND_API

void audio_monitor_init(void);
void audio_monitor_cleanup(void);

#endif /* ENABLE_FRONTEND_API */

#endif /* AUDIO_MONITOR_H */
