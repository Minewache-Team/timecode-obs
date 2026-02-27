/*
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
