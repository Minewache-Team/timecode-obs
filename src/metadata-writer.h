/*
 * metadata-writer.h - Recording metadata sidecar file writer
 *
 * Automatically creates a JSON sidecar file next to each OBS recording,
 * containing camera identity, timecodes, NTP sync quality, and settings.
 * Requires ENABLE_FRONTEND_API for recording start/stop event hooks.
 */

#ifndef METADATA_WRITER_H
#define METADATA_WRITER_H

#ifdef ENABLE_FRONTEND_API

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque metadata writer context */
typedef struct metadata_writer metadata_writer_t;

/*
 * Create a metadata writer and register recording event callbacks.
 * Call once per LTC source instance.
 */
metadata_writer_t *metadata_writer_create(void);

/*
 * Update metadata fields. Call whenever settings change.
 * All string parameters are copied internally.
 */
void metadata_writer_set_info(metadata_writer_t *mw, int camera_id,
			      const char *framerate_str,
			      const char *ntp_server, bool ntp_synced,
			      int64_t ntp_offset_ms,
			      const char *sync_method_str);

/*
 * Update the current timecode string for sidecar output.
 * Called from video_tick to keep timecode current.
 */
void metadata_writer_set_timecode(metadata_writer_t *mw, const char *tc_str);

/*
 * Destroy the metadata writer and unregister callbacks.
 */
void metadata_writer_destroy(metadata_writer_t *mw);

#ifdef __cplusplus
}
#endif

#endif /* ENABLE_FRONTEND_API */
#endif /* METADATA_WRITER_H */
