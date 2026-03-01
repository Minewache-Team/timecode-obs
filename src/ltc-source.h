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
 * ltc-source.h - OBS audio source for LTC timecode output
 *
 * This is the OBS integration layer. It depends on OBS headers.
 */

#ifndef LTC_SOURCE_H
#define LTC_SOURCE_H

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register the LTC source with OBS */
void ltc_source_register(void);

#ifdef __cplusplus
}
#endif

#endif /* LTC_SOURCE_H */
