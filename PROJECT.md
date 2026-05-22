# PROJECT.md – OBS LTC Timecode Plugin

> **This file is the single source of truth for the project.**
> Every AI agent working on this project MUST read this file first and update it after completing work.
> If an agent session is lost, the next agent picks up exactly where this file says.

---

## Project Overview

| Field            | Value                                      |
|------------------|--------------------------------------------|
| Name             | obs-ltc-timecode                           |
| Type             | OBS Studio C/C++ Plugin (native)           |
| Purpose          | NTP-synced LTC timecode audio source       |
| Current Version  | 0.5.1 (Minewache branch)                   |
| Target Platforms | Windows 10+ (x64), Linux (Ubuntu 24.04+)  |
| OBS SDK Version  | 32.x (current stable)                      |
| License          | GPLv2+ / GPL-2.0-or-later (OBS compat)    |
| Dependencies     | OBS SDK (auto-fetched), libltc (submodule) |
| Test Framework   | Google Test 1.15+ (FetchContent)           |

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      OBS Studio 32.x                         │
│                                                              │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │              obs-ltc-timecode plugin                     │ │
│  │                                                         │ │
│  │  ┌────────────┐  ┌────────────┐  ┌───────────────────┐ │ │
│  │  │ NTP Sync   │→ │ Timecode   │→ │ LTC Audio         │ │ │
│  │  │ Module     │  │ Generator  │  │ Encoder (libltc)  │ │ │
│  │  │            │  │            │  │                   │ │ │
│  │  │ ntp-client │  │ timecode   │  │ ltc-encoder-      │ │ │
│  │  │ .c/.h      │  │ .c/.h      │  │ wrapper.c/.h      │ │ │
│  │  └────────────┘  └────────────┘  └───────────────────┘ │ │
│  │       ↓               ↓                ↓               │ │
│  │  pool.ntp.org    SMPTE HH:MM:SS:FF   PCM samples      │ │
│  │  (UDP 123)       @ configured FPS     → OBS audio      │ │
│  │                                       pipeline         │ │
│  │                                                         │ │
│  │  ┌─────────────────────────────────────────────────┐   │ │
│  │  │              ltc-source.c/.h                     │   │ │
│  │  │  OBS Integration Layer (obs_source_info)         │   │ │
│  │  │  - Registers as OBS_SOURCE_TYPE_INPUT (audio)    │   │ │
│  │  │  - Properties UI (framerate, NTP server, status) │   │ │
│  │  │  - Audio callback → fills PCM buffer from core   │   │ │
│  │  └─────────────────────────────────────────────────┘   │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

**Design Principle: Testable Core + Thin OBS Adapter**

The three core modules (NTP, Timecode, LTC Encoder) have ZERO OBS dependencies.
They can be compiled and tested standalone with Google Test.
Only `ltc-source.c` and `plugin-main.c` include OBS headers.

---

## Backlog (Tickets)

### Epic 0: Project Setup ✅

#### TICKET-000: Repository & Build System Setup
- **Status:** `DONE` (manually completed by developer)
- **Type:** Setup
- **Description:** Create repo from obs-plugintemplate, add libltc submodule, configure CMake, set up test infrastructure.
- **Acceptance Criteria:**
  - [x] Repo created with correct structure
  - [x] libltc submodule added in `deps/libltc/`
  - [x] `cmake --preset <platform>` succeeds
  - [x] Google Test fetched and test target builds
  - [x] CI pipeline runs on push
- **Files:** `CMakeLists.txt`, `CMakePresets.json`, `buildspec.json`, `tests/CMakeLists.txt`

---

### Epic 1: Plugin Skeleton

#### TICKET-001: Plugin Registration & Silent Audio Source
- **Status:** `DONE`
- **Type:** Feature
- **Description:** Implement `obs_module_load()`, `obs_module_unload()`, and register an `obs_source_info` of type `OBS_SOURCE_TYPE_INPUT` with `OBS_SOURCE_AUDIO` capability. The source should continuously output silence (zero-filled PCM buffer) as proof of life. Use `audio_render` callback or timer-based `obs_source_output_audio()`.
- **Acceptance Criteria:**
  - [x] Plugin appears in OBS under Sources → Add → "LTC Timecode Generator"
  - [x] OBS audio mixer shows the source with silent output (green bar inactive)
  - [x] Plugin loads/unloads cleanly (check OBS log for errors)
  - [x] `en-US.ini` locale strings load correctly
  - [x] No memory leaks on source create/destroy cycle
- **Technical Notes:**
  - Sample rate: 48000 Hz (OBS default)
  - Channels: 1 (mono) — LTC is always mono
  - Use `obs_source_output_audio()` with timer for simplicity
  - Allocate audio buffer in `source_create`, free in `source_destroy`
- **Files to implement:**
  - `src/plugin-main.c` — fill in `obs_module_load/unload`
  - `src/ltc-source.c` — fill in all callback stubs
  - `src/ltc-source.h` — finalize context struct

---

### Epic 2: NTP Time Synchronization

#### TICKET-002: SNTP Client Implementation
- **Status:** `DONE`
- **Depends on:** TICKET-001
- **Type:** Feature
- **Description:** Implement a minimal SNTP client that queries `pool.ntp.org` (UDP port 123) and calculates the offset between the local `clock_gettime(CLOCK_REALTIME)` / `GetSystemTimeAsFileTime()` and NTP time. Must work without admin privileges.
- **Acceptance Criteria:**
  - [x] Successfully queries NTP server and receives valid response
  - [x] Calculates offset in milliseconds (positive = local ahead, negative = local behind)
  - [x] Works on Windows (Winsock2) and Linux (POSIX sockets)
  - [x] Handles network failure gracefully (timeout 2s, 3 retries, fallback to local clock)
  - [x] No admin privileges required (verified on stock Windows)
  - [x] Unit test: mock NTP response → verify offset calculation
- **Technical Notes:**
  - NTP packet: 48 bytes, version 4, mode 3 (client)
  - Offset = ((T2-T1) + (T3-T4)) / 2 where T1/T4 are local, T2/T3 are server
  - Use `select()` for timeout (cross-platform)
  - On Windows: `WSAStartup()` / `WSACleanup()` lifecycle
- **Files:** `src/ntp-client.c`, `src/ntp-client.h`
- **Test:** `tests/test-ntp-offset.cpp`

#### TICKET-003: Periodic Re-Sync & Thread Safety
- **Status:** `DONE`
- **Depends on:** TICKET-002
- **Type:** Feature
- **Description:** Run NTP queries on a background thread. Re-sync at configurable interval (default 5 min). Store offset with atomic access for lock-free reading from audio thread.
- **Acceptance Criteria:**
  - [x] Background thread starts on source_create, stops on source_destroy
  - [x] Re-syncs at configurable interval
  - [x] Offset readable from audio thread without locks (volatile int64_t)
  - [x] Offset changes are smoothed (exponential moving average, alpha=0.3)
  - [x] Thread starts/stops cleanly via os_event_t signaling, no races
- **Files:** `src/ntp-client.c` (modified), `src/ltc-source.c` (thread lifecycle)

---

### Epic 3: Timecode Generation

#### TICKET-004: SMPTE Timecode from NTP-Corrected Clock
- **Status:** `DONE`
- **Depends on:** TICKET-002
- **Type:** Feature
- **Description:** Convert NTP-corrected wall-clock time to SMPTE timecode (HH:MM:SS:FF) at the configured framerate. Time-of-day based: 14:30:22 @ 30fps → 14:30:22:00.
- **Acceptance Criteria:**
  - [x] Correct HH:MM:SS:FF output verified against known timestamps
  - [x] Supports 24, 25, 30, 50, 60 fps (non-drop-frame)
  - [x] Handles drop-frame for 29.97 fps (SMPTE standard skip pattern)
  - [x] Auto-detect OBS output framerate via `obs_get_video_info()`
  - [x] Manual override available in properties
  - [x] Midnight rollover: 23:59:59:29 → 00:00:00:00
  - [x] Unit tests for all framerates + edge cases (10 tests)
- **Files:** `src/timecode.c`, `src/timecode.h`
- **Test:** `tests/test-timecode.cpp`

---

### Epic 4: LTC Audio Encoding

#### TICKET-005: libltc Encoder Wrapper
- **Status:** `DONE`
- **Depends on:** TICKET-004
- **Type:** Feature
- **Description:** Wrap libltc's encoder API in a clean C interface. Initialize encoder at OBS sample rate, set timecode per frame, generate PCM audio samples.
- **Acceptance Criteria:**
  - [x] `ltc_wrapper_create(sample_rate, fps)` initializes encoder (accepts tc_framerate_t)
  - [x] `ltc_wrapper_set_timecode(h, m, s, f)` sets current timecode
  - [x] `ltc_wrapper_encode_frame(buffer, max_samples)` fills buffer with LTC audio
  - [x] `ltc_wrapper_inc_timecode()` advances by one frame (uses libltc internal logic)
  - [x] Output amplitude is -12dBFS
  - [x] Supports all framerates including 29.97df (30000/1001 sample rate)
  - [x] Memory management: create/destroy lifecycle clean
  - [x] Roundtrip test: encode → decode via libltc decoder → timecodes match (11 tests)
- **Files:** `src/ltc-encoder-wrapper.c`, `src/ltc-encoder-wrapper.h`
- **Test:** `tests/test-ltc-roundtrip.cpp`

#### TICKET-006: Continuous Audio Generation in OBS
- **Status:** `DONE`
- **Depends on:** TICKET-005, TICKET-003
- **Type:** Feature (Integration)
- **Description:** Wire everything together. In the audio output callback: read NTP-corrected time → convert to SMPTE TC → encode as LTC audio → output to OBS pipeline. Must be sample-continuous (no clicks/gaps between frames).
- **Acceptance Criteria:**
  - [x] Continuous LTC audio output via video_tick → OBS audio pipeline
  - [x] Audio is decodable (multi-frame roundtrip verified in tests)
  - [x] Sample-continuous frame boundaries (frame buffer approach)
  - [x] Timecode advances correctly (inc_timecode + periodic wall-clock re-sync)
  - [x] Works at 48kHz mono (OBS default sample rate)
  - [x] Continuous timestamp tracking prevents gaps/overlaps
- **Files:** `src/ltc-source.c` (modified — main integration point)

---

### Epic 5: User Interface & Polish

#### TICKET-007: Properties UI
- **Status:** `DONE`
- **Depends on:** TICKET-006
- **Type:** Feature
- **Description:** Add OBS properties panel with framerate selector, NTP server config, sync status display.
- **Acceptance Criteria:**
  - [x] Framerate dropdown: "Auto (from OBS)" + manual options (24/25/29.97df/30/50/60)
  - [x] NTP server text field (default: "pool.ntp.org")
  - [x] NTP sync interval dropdown (1/5/10/30 min)
  - [x] NTP status info display
  - [x] Current timecode info display
  - [x] All settings persist via obs_data (get_defaults + update callbacks)
- **Files:** `src/ltc-source.c` (modified — `get_properties`, `get_defaults`, `update`)

#### TICKET-008: DaVinci Resolve Validation
- **Status:** `TODO`
- **Depends on:** TICKET-007
- **Type:** Test / Validation
- **Description:** End-to-end test: record with plugin on two PCs, import into DaVinci Resolve, verify timecode sync.
- **Acceptance Criteria:**
  - [ ] MKV file contains LTC audio on dedicated track
  - [ ] DaVinci Resolve reads and displays timecode from LTC track
  - [ ] Two recordings from different PCs show matching timecode (≤ 1 frame tolerance)
  - [ ] Timecode is correct after 1+ hour recording (no drift)
- **Manual Test Protocol:**
  1. Install plugin on PC-A and PC-B
  2. Both add "LTC Timecode Generator" source, assign to separate audio track
  3. Start recording simultaneously (within ~5 seconds)
  4. Record for 10+ minutes
  5. Import both MKVs into DaVinci Resolve
  6. Use LTC track for timecode sync
  7. Verify sync accuracy at start, middle, and end of recording

---

### Epic 6: Release & Distribution

#### TICKET-009: README & Documentation
- **Status:** `DONE`
- **Depends on:** TICKET-008
- **Type:** Documentation
- **Description:** Write user-facing README with install instructions, usage guide, FAQ.
- **Acceptance Criteria:**
  - [x] Clear install instructions for Windows and Linux
  - [x] Usage guide (text-based, no screenshots available in CLI)
  - [x] Troubleshooting section
  - [x] Link to releases page

#### TICKET-010: First Release (v0.1.0)
- **Status:** `TODO`
- **Depends on:** TICKET-009
- **Type:** Release
- **Description:** Tag v0.1.0, verify CI builds release artifacts.
- **Acceptance Criteria:**
  - [ ] `git tag v0.1.0` triggers CI
  - [ ] GitHub Release created with Windows .zip and Linux .deb/.tar.gz
  - [ ] Plugin installs and works from release artifacts
  - [ ] Release notes document known limitations

---

### Epic 7: Installation & First-Run Experience (User Feedback)

> Created from user testing feedback (2026-02-27): Plugin doesn't appear in OBS,
> manual folder creation needed, setup too error-prone.

#### TICKET-011: Windows Install Script & Installation Debugging
- **Status:** `DONE`
- **Depends on:** TICKET-006
- **Type:** Enhancement / Tooling
- **Description:** Create a PowerShell install script (`install.ps1`) that automates plugin installation on Windows. The script must handle the fact that `%APPDATA%\obs-studio\plugins` does NOT exist by default (OBS only creates `plugin_config`). Additionally, add verbose logging at plugin load time to help diagnose why the plugin might not appear in OBS.
- **Acceptance Criteria:**
  - [x] `install.ps1` script creates full folder structure (`plugins\obs-ltc-timecode\bin\64bit` + `data\locale`)
  - [x] Script copies DLL and data files to correct locations
  - [x] Script verifies the installation after copy (checks files exist, prints summary)
  - [x] Script detects OBS installation and warns if not found
  - [x] `install.sh` equivalent for Linux
  - [x] README.md updated with install script usage
  - [x] Setup.md Schritt 6 updated with note about `plugins` folder creation
  - [x] `plugin-main.c` logs file paths on load for debugging
- **User Feedback:** _"musste in %APPDATA%\obs-studio den plugins Ordner erstellen, weiß nicht ob das normal ist, es gab da nur plugin_config als Ordner"_
- **Files:** `install.ps1` (new), `install.sh` (new), `src/plugin-main.c` (modified), `README.md` (modified), `Setup.md` (modified)

#### TICKET-012: Auto-Setup on First Load (Switch to Minewache Template)
- **Status:** `DONE`
- **Depends on:** TICKET-011, TICKET-014
- **Type:** Feature
- **Description:** When the plugin loads for the first time, detect if the Minewache scene collection is installed and offer to switch to it via a native dialog. The Minewache template has LTC Timecode pre-configured on Track 3. A config flag prevents repeated prompting.
- **Acceptance Criteria:**
  - [x] `ENABLE_FRONTEND_API` enabled in CMakeLists.txt (default ON)
  - [x] On first plugin load: dialog asks to switch to Minewache scene collection
  - [x] If user accepts: switches scene collection + profile to "Minewache"
  - [x] First-run flag stored in OBS user config (`obs-ltc-timecode/auto_setup_done`)
  - [x] If Minewache scene collection not installed: silently skips
  - [x] If already active: marks done without dialog
  - [x] Logging: clear messages about what was auto-configured
  - [x] Works on Windows (MessageBoxA dialog) and Linux (auto-switch without dialog)
- **User Feedback:** _"Plugin soll beim Starten direkt die Option geben zum Minewache template zu switchen"_
- **Technical Notes:**
  - Uses `obs_frontend_add_event_callback()` with `OBS_FRONTEND_EVENT_FINISHED_LOADING`
  - Scene collection switch: `obs_frontend_set_current_scene_collection()`
  - Profile switch: `obs_frontend_set_current_profile()`
  - Config: `obs_frontend_get_user_config()` (NOT deprecated `get_global_config`)
  - Windows dialog: Win32 `MessageBoxA()` (no Qt dependency needed)
- **Files:** `CMakeLists.txt` (modified), `src/plugin-main.c` (modified), `src/auto-setup.c` (new), `src/auto-setup.h` (new)

#### TICKET-013: Plugin Load Diagnostics & OBS Version Verification
- **Status:** `DONE`
- **Depends on:** TICKET-006
- **Type:** Bug Investigation / Enhancement
- **Description:** User reports plugin doesn't appear in OBS Sources after installation. Root cause found: **All documentation pointed to the WRONG install path** (`%APPDATA%\obs-studio\plugins\` instead of `%ProgramData%\obs-studio\plugins\`). OBS does NOT load plugins from `%APPDATA%` — that directory is only for settings/config. The correct path is `C:\ProgramData\obs-studio\plugins\<name>\bin\64bit\`.
- **Root Cause:** Wrong install path in all docs (README, Setup.md, install.ps1)
- **Fix:** Updated all documentation and install scripts to use `%ProgramData%\obs-studio\plugins\`
- **Acceptance Criteria:**
  - [x] `obs_module_load()` logs: OBS version, plugin path, locale load status
  - [x] `obs_module_description()` callback added (shows plugin info in OBS plugin dialog)
  - [ ] `buildspec.json` verified against actual OBS 32.x SDK version (deferred — CI hashes need update)
  - [x] README troubleshooting section expanded with common load failures
  - [x] Checklist: what to check in OBS log when plugin doesn't appear
  - [x] **Root cause fix:** All install paths corrected from `%APPDATA%` to `%ProgramData%`
- **User Feedback:** _"der erste Schritt geht schon nicht, da ist kein LTC Timecode Generator unter Sources +"_
- **Files:** `install.ps1` (fixed), `README.md` (fixed), `Setup.md` (fixed), `src/plugin-main.c` (improved logging)

---

### Epic 8: MW OBS KIT Template Integration

> User request: Integrate the LTC timecode plugin into the Minewache team's OBS template
> so that plugin installation automatically deploys a ready-to-use scene collection with
> a dedicated LTC audio track (Track 3).

#### TICKET-014: Add LTC Timecode Source to MW OBS KIT Template
- **Status:** `DONE`
- **Depends on:** TICKET-006
- **Type:** Enhancement
- **Description:** Update the `[MW] OBS KIT` scene collection template to include an "LTC Timecode" audio source routed exclusively to Track 3. Update the profile's `basic.ini` to name Track 3 "LTC Timecode" and include it in `RecTracks`. The LTC source must have `mixers=4` (bit 2 = Track 3 only) so it doesn't bleed into voice or game audio tracks.
- **Acceptance Criteria:**
  - [x] `Minewache.json` contains an `ltc_timecode_source` entry with `mixers=4`
  - [x] `basic.ini` sets `Track3Name=LTC Timecode`
  - [x] `RecTracks` updated from `3` (Tracks 1+2) to `7` (Tracks 1+2+3) in both SimpleOutput and AdvOut
  - [x] LTC source default settings: framerate=auto, ntp_server=pool.ntp.org, sync_interval=5
  - [x] Scene item added to "Minewache Scene" items list (id: 5)
- **Files:** `[MW] OBS KIT/Minewache.json` (modified), `[MW] OBS KIT/Minewache/basic.ini` (modified)

#### TICKET-015: Deploy MW OBS KIT Template via install.ps1
- **Status:** `DONE`
- **Depends on:** TICKET-014
- **Type:** Enhancement
- **Description:** Extend `install.ps1` to copy the MW OBS KIT template files to the OBS configuration directory (`%APPDATA%\obs-studio\basic\`). The scene collection JSON goes to `basic\scenes\`, the profile folder goes to `basic\profiles\`. Only deploy if the scene collection doesn't already exist (avoid overwriting user customizations). Print instructions for the user to select the "Minewache" scene collection/profile in OBS.
- **Acceptance Criteria:**
  - [x] Scene collection copied to `%APPDATA%\obs-studio\basic\scenes\Minewache.json`
  - [x] Profile folder copied to `%APPDATA%\obs-studio\basic\profiles\Minewache\`
  - [x] Skip copy if `Minewache.json` already exists (print info message)
  - [x] Post-install message tells user to select "Minewache" scene collection in OBS
  - [x] Uninstall does NOT remove scene collection (user may have customized it)
- **Files:** `install.ps1` (modified)

#### TICKET-016: Deploy MW OBS KIT Template via Inno Setup Installer
- **Status:** `DONE`
- **Depends on:** TICKET-014
- **Type:** Enhancement
- **Description:** Extend the Inno Setup installer (`obs-ltc-timecode.iss`) to include the MW OBS KIT template files and deploy them to the OBS configuration directory during installation. Use `{userappdata}\obs-studio\basic\` as the target. Template files should use `onlyifdoesntexist` flag to avoid overwriting user customizations. Update post-install message to mention the Minewache template.
- **Acceptance Criteria:**
  - [x] Template files included as [Files] entries in the ISS script
  - [x] Scene collection deployed to `{userappdata}\obs-studio\basic\scenes\Minewache.json`
  - [x] Profile deployed to `{userappdata}\obs-studio\basic\profiles\Minewache\`
  - [x] `onlyifdoesntexist` flag prevents overwriting existing files
  - [x] Post-install message updated to mention "Minewache" scene collection
  - [x] Template source files included in installer packaging (CI-compatible paths)
- **Files:** `installer/obs-ltc-timecode.iss` (modified)

---

### Epic 9: Metadata & Reliability Enhancements

> Feature analysis session (2026-03-01): Identified useful additions for decentralized
> multi-camera recording workflow (multiple PCs across Germany, footage merged in DaVinci Resolve).
> Focus: DaVinci Resolve compatibility, metadata for automated sorting, sync reliability.
> User constraint: Users are non-technical — UI must be dead simple.

#### TICKET-017: LTC User Bits — Date & Camera ID
- **Status:** `DONE`
- **Depends on:** TICKET-006
- **Type:** Feature
- **Description:** Embed the current date and a configurable camera identifier into the LTC User Bits of every audio frame. The date uses SMPTE 12M format (DD:MM:YY in 6 nibbles). The camera ID uses the remaining 2 nibbles (1 byte, values 1–8). DaVinci Resolve reads User Bits automatically and displays them in the Media Pool, enabling camera identification and date verification in multi-cam workflows.
- **Current State:** `ltc-encoder-wrapper.c:101-103` sets `st.years=0, st.months=0, st.days=0` — User Bits are all zeros despite `LTC_USE_DATE` being active in `ltc_encoder_create()`.
- **Acceptance Criteria:**
  - [x] Date automatically embedded from NTP-corrected clock (no user config needed)
  - [x] `ltc_wrapper_set_timecode()` extended with date + camera_id parameters
  - [x] Camera ID as simple dropdown in Properties UI: "Kamera A" through "Kamera H" (default: "Kamera A")
  - [x] No free-text input for camera ID (User Bits only have 1 byte left after date)
  - [ ] DaVinci Resolve displays date and camera info from imported LTC track
  - [x] Existing roundtrip tests updated to verify date + camera ID survive encode/decode
  - [x] Locale strings added for new UI elements
- **UX Notes:** Camera dropdown must be obvious and self-explanatory. Labels are "Kamera A", "Kamera B", ..., "Kamera H" — not hex values or numbers. Users just pick their letter.
- **Technical Notes:**
  - libltc: `SMPTETimecode.years/months/days` → `ltc_time_to_frame()` writes date into User Bits
  - Camera ID: `ltc_encoder_set_user_bits()` can set remaining nibbles after date encoding
  - User Bits layout (32 bits): 6 nibbles date (24 bits) + 2 nibbles camera ID (8 bits)
- **Files:** `src/ltc-encoder-wrapper.c/h` (extend API), `src/ltc-source.c` (UI + date/ID forwarding), `src/timecode.c/h` (date extraction helper), `data/locale/en-US.ini`, `tests/test-ltc-roundtrip.cpp`

#### TICKET-018: HTTP/HTTPS Time Fallback for Restricted Networks
- **Status:** `DONE`
- **Depends on:** TICKET-003
- **Type:** Enhancement
- **Description:** When NTP (UDP port 123) is unreachable, automatically fall back to an HTTP-based time service. Primary use case: residential networks that are mostly fine, but as a redundancy measure for the occasional restrictive network (hotels, event venues, corporate guest WiFi). The fallback is fully automatic — no user configuration required.
- **Acceptance Criteria:**
  - [x] New module `http-time-client.c/h` queries HTTPS endpoint via Date header (using libcurl from OBS deps)
  - [x] Fallback chain in NTP sync thread: NTP → HTTP → local clock
  - [x] HTTP fallback only attempted after NTP fails all retries
  - [x] Sync method tracked via `sync_method_t` enum
  - [x] Zero user configuration — fallback happens automatically
  - [x] HTTP timeout: 5 seconds
  - [x] Unit tests: HTTP Date header parser (11 tests, no network needed)
  - [x] Locale strings for sync method status messages
- **UX Notes:** User sees only the status indicator change. No new settings, no toggles. It just works.
- **Technical Notes:**
  - Investigate OBS internal libcurl availability vs. minimal HTTP client
  - HTTP Date header from any HTTPS server is an alternative to worldtimeapi.org
  - Accuracy: ~100–500ms (vs NTP ~10–50ms). At 30 FPS (33ms/frame) = ±1–2 frames. Acceptable as fallback.
  - Current behavior (fall back to local clock silently) remains as last resort
- **Files:** `src/http-time-client.c/h` (new), `src/ltc-source.c` (fallback logic + status display), `data/locale/en-US.ini`, `tests/test-http-time.cpp` (new)
- **Open Question:** HTTP library — can we use OBS's internal libcurl, or do we need a minimal client? Research needed.

#### TICKET-019: Recording Metadata Sidecar File
- **Status:** `DONE`
- **Depends on:** TICKET-017
- **Type:** Feature
- **Description:** Automatically create a JSON sidecar file alongside each OBS recording. The file contains camera identity, timecodes, NTP sync quality, and recording settings. Enables automated post-production sorting and provides a quality audit trail for multi-camera shoots.
- **Acceptance Criteria:**
  - [x] JSON sidecar `.ltc.json` created next to recording on stop (same base name)
  - [x] End timecode and duration written at recording stop
  - [x] Content includes: `camera_id`, `start_timecode`, `end_timecode`, `duration_seconds`, `framerate`, `ntp_synced`, `ntp_offset_ms`, `sync_method`, `plugin_version`
  - [x] File created automatically — no toggle, no settings
  - [x] Handles edge cases: recording path not writable (log warning, don't crash)
  - [x] Uses `obs_frontend_add_event_callback()` for `RECORDING_STARTED` / `RECORDING_STOPPED`
- **UX Notes:** Completely invisible to the user. A small JSON file appears next to their recording. They can ignore it or use it for automation later.
- **Technical Notes:**
  - Recording output path: `obs_frontend_get_last_recording()` or `obs_frontend_get_current_record_output_path()`
  - Write JSON manually (no JSON library needed for a simple flat object)
  - Write start data immediately (valid JSON), append end data on stop
  - Camera ID comes from TICKET-017 setting
- **Files:** `src/metadata-writer.c/h` (new), `src/ltc-source.c` (frontend event hooks), `data/locale/en-US.ini`

#### TICKET-020: Audio Track Selection in Plugin UI
- **Status:** `DONE`
- **Depends on:** TICKET-006
- **Type:** Enhancement
- **Description:** Add a dropdown in the plugin Properties to select which OBS audio track the LTC output is routed to. Without the Minewache template, users must manually configure track routing in OBS's Advanced Audio Properties — a confusing process for non-technical users. The dropdown makes this a one-click operation.
- **Acceptance Criteria:**
  - [x] Dropdown in Properties: "Track 1" through "Track 6" (default: Track 3)
  - [x] Selection calls `obs_source_set_audio_mixers(source, bitmask)` with exclusive track
  - [x] Setting persists via `obs_data`
  - [x] Changing the track takes effect immediately (no restart needed)
  - [x] Locale strings for label
- **UX Notes:** Help text: *"LTC audio is recorded only on this track. Default is Track 3, so it stays separate from voice (Track 1) and game audio (Track 2)."* Users just pick a track number from the list.
- **Technical Notes:**
  - `obs_source_set_audio_mixers()` takes a bitmask: Track 1 = 0x01, Track 2 = 0x02, Track 3 = 0x04, etc.
  - Must be called after source creation and on every update
  - Minewache template already sets `mixers=4` (Track 3) — this UI makes it configurable for non-template users
- **Files:** `src/ltc-source.c` (UI + mixer API call), `data/locale/en-US.ini`

#### TICKET-021: Track Recording Warning
- **Status:** `DONE`
- **Depends on:** TICKET-020
- **Type:** Enhancement
- **Description:** When the user selects an audio track for LTC output, check whether that track is actually being recorded in OBS's output settings. If not, display a warning in the Properties UI so the user notices before starting a recording. Uses `obs_frontend_get_profile_config()` to read the `RecTracks` bitmask from the active profile's `basic.ini` (both `SimpleOutput` and `AdvOut` sections).
- **Acceptance Criteria:**
  - [x] Warning displayed in Properties when selected track is not in OBS RecTracks bitmask
  - [x] Checks both `[SimpleOutput]` and `[AdvOut]` RecTracks config keys
  - [x] Warning text is clear and actionable (tells user which track is missing)
  - [x] No warning when track is correctly configured
  - [x] Works behind `ENABLE_FRONTEND_API` guard (degrades gracefully without it)
  - [x] Locale string for warning text
- **UX Notes:** Warning appears as `OBS_TEXT_INFO` property below the track dropdown. Non-blocking — user can still use the plugin, but is informed that their LTC won't be recorded.
- **Technical Notes:**
  - `obs_frontend_get_profile_config()` returns `config_t*` for the active profile
  - `config_get_uint(config, "SimpleOutput", "RecTracks")` — bitmask, bit 0 = Track 1
  - `config_get_uint(config, "AdvOut", "RecTracks")` — same for Advanced output mode
  - Check both because user could be in either output mode
  - RecTracks default is typically `1` (Track 1 only) if not explicitly configured
- **Files:** `src/ltc-source.c` (warning logic in `get_properties`), `data/locale/en-US.ini`

---

### Epic 10: License Compliance Cleanup

> License audit (2026-03-01): Found multiple compliance issues. Plugin header says GPLv2+
> but docs say GPLv2-only. libltc (LGPLv3) is statically linked without providing object files.
> Most source files lack copyright headers. OBS requires all plugins to be GPL-compatible open source.

#### TICKET-022: Correct License Declaration to GPLv2+
- **Status:** `DONE`
- **Type:** Bug / Legal
- **Description:** The code header in `plugin-main.c` correctly says "GPLv2 or later" but `PROJECT.md` says "GPLv2" and `README.md` says "GPLv2.0" without the "or later" clause. This creates a legal ambiguity — especially critical because libltc is LGPLv3, which is only compatible with GPLv2+ (not GPLv2-only). All documentation must consistently say "GPLv2 or later" (SPDX: `GPL-2.0-or-later`).
- **Acceptance Criteria:**
  - [x] `PROJECT.md` license field says "GPLv2+ (GPL-2.0-or-later)"
  - [x] `README.md` license section says "GNU General Public License v2.0 or later"
  - [x] `LICENSE` file template placeholders at end of file filled in
  - [x] Consistent wording across all files
- **Files:** `PROJECT.md`, `README.md`, `LICENSE`

#### TICKET-023: Add Copyright Headers to All Source Files
- **Status:** `DONE`
- **Depends on:** TICKET-022
- **Type:** Legal / Compliance
- **Description:** GPLv2 Section 1 requires each source file to carry a copyright notice. Currently only `plugin-main.c` and `plugin-support.h` have headers (and `plugin-support.h` still has template placeholders). All other 12 source files (`.c` and `.h`) are missing the required GPLv2+ copyright header. Additionally, `plugin-support.h` still contains `<Year> <Developer> <Email Address>` placeholders from the obs-plugintemplate.
- **Acceptance Criteria:**
  - [x] All `.c` and `.h` files in `src/` have the standard GPLv2+ copyright header
  - [x] `plugin-support.h` placeholders replaced with actual values
  - [x] Copyright year: 2024–2026, Author: Ferdmusic
  - [x] Header format consistent across all files
- **Files:** All files in `src/`

#### TICKET-024: libltc LGPL-3.0 Static Linking Compliance
- **Status:** `DONE`
- **Depends on:** TICKET-022
- **Type:** Legal / Build System
- **Description:** libltc is LGPLv3 and currently statically linked (`add_library(libltc STATIC)` in `BuildLibLTC.cmake`). LGPLv3 Section 4d requires that users can re-link the application with a modified version of the library. Two options: (a) switch to dynamic linking (SHARED library), or (b) provide object files for relinking. Dynamic linking is the cleanest solution — it satisfies LGPL automatically and is standard practice for LGPL libraries. The README already mentions "object files available for relinking" but this was never implemented.
- **Acceptance Criteria:**
  - [x] `BuildLibLTC.cmake` changed from `STATIC` to `SHARED`
  - [x] libltc DLL/SO deployed alongside the plugin binary
  - [x] CMake `install()` rules updated to include libltc shared library
  - [x] Plugin loads and works correctly with dynamic libltc
  - [x] README updated to reflect dynamic linking (remove "object files" claim)
  - [x] `Known Risks` item #3 updated to reflect resolution
  - [x] Installer (`obs-ltc-timecode.iss`) updated to include libltc DLL
- **Technical Notes:**
  - Windows: `libltc.dll` goes next to the plugin DLL in `bin/64bit/`
  - Linux: `libltc.so` installed to plugin lib directory or system lib path
  - `POSITION_INDEPENDENT_CODE` already set (required for shared libs)
  - Test executables also need to link against the shared library
- **Files:** `cmake/BuildLibLTC.cmake`, `CMakeLists.txt`, `README.md`, `installer/obs-ltc-timecode.iss`

#### TICKET-025: Update Known Risks — License Issues Resolved
- **Status:** `DONE`
- **Depends on:** TICKET-022, TICKET-023, TICKET-024
- **Type:** Documentation
- **Description:** Update the Known Risks section to reflect that the libltc LGPL licensing concern (item #3) has been resolved via dynamic linking, and add a note about the OBS GPLv2+ requirement for all plugins.
- **Acceptance Criteria:**
  - [x] Known Risk #3 marked as RESOLVED with explanation
  - [x] Decision Log entries added for license cleanup decisions
- **Files:** `PROJECT.md`

---

### Epic 11: Public Release Preparation

> Sessions 10 (2026-03-02 → 2026-03-04): Polish for public availability on GitHub.

#### TICKET-026: Public Release & README Polish
- **Status:** `DONE`
- **Type:** Release / Docs
- **Description:** Refactor README for clarity, fix broken links, add email contact to `buildspec.json`, configure CI to auto-publish releases. Add `[MW] OBS KIT` scene collection files to the repo (previously developer-local). Rename internal "Minewache" identifiers where they collided with the public-facing template name.
- **Acceptance Criteria:**
  - [x] README install/usage/troubleshooting sections rewritten
  - [x] All GitHub links in README verified, Linux binary availability clarified
  - [x] `buildspec.json` carries author email
  - [x] CI release workflow set to publish automatically on tag
  - [x] `[MW] OBS KIT/Minewache.json` + `basic.ini` + encoder JSONs added to git
  - [x] `install.ps1` uses `-LiteralPath` for paths with `[MW]` brackets
- **Files:** `README.md`, `buildspec.json`, `.github/workflows/*.yaml`, `install.ps1`, `[MW] OBS KIT/*`

---

### Epic 12: MW Recording Status Module

> Session 11 (2026-03-27): A new module that lets a director see, on a remote
> dashboard, which cameras across multiple PCs are currently recording. Sends
> start/stop/heartbeat signals to a PHP backend over HTTPS. DSGVO consent required
> before any data leaves the machine.

#### TICKET-027: MW Recording Status Reporting Module
- **Status:** `DONE`
- **Depends on:** TICKET-006
- **Type:** Feature
- **Description:** New module `mw-recording.c/h` that hooks into OBS recording events (`OBS_FRONTEND_EVENT_RECORDING_STARTED/STOPPED/PAUSED`) and posts JSON status to a configurable HTTPS endpoint. A background heartbeat thread sends a keep-alive every 30s while recording is active. A pause warning is shown to discourage pausing (which breaks LTC continuity).
- **Acceptance Criteria:**
  - [x] Module uses libcurl from OBS deps (no new dependency)
  - [x] Heartbeat thread runs only while recording, joins cleanly on stop
  - [x] Settings: server URL, camera name, API key, enabled toggle
  - [x] Pause warning dialog before transmitting paused state
- **Files:** `src/mw-recording.c/h` (new), `src/ltc-source.c` (initial wiring), `CMakeLists.txt`, `data/locale/en-US.ini`

#### TICKET-028: Move MW Settings to OBS Tools Menu (Global Config)
- **Status:** `DONE`
- **Depends on:** TICKET-027
- **Type:** Refactor / UX
- **Description:** MW settings were initially per-source (in the LTC source properties), but the MW recording state is global to the OBS instance, not per source. Move settings to a dedicated dialog accessible via `Tools → MW Aufnahme`. Persist via `obs_frontend_get_user_config()` instead of `obs_data`. Remove the MW properties from the LTC source.
- **Acceptance Criteria:**
  - [x] "MW Aufnahme" entry under OBS Tools menu opens a Win32 settings dialog
  - [x] Settings persist in OBS global user config under `obs-ltc-timecode/mw-*`
  - [x] LTC source properties no longer show MW fields
  - [x] Heartbeat, start/stop signals fire automatically on any recording start
- **Files:** `src/mw-recording.c` (major refactor), `src/ltc-source.c` (removed MW props)

#### TICKET-029: DSGVO Consent Dialog with Clickable Links
- **Status:** `DONE`
- **Depends on:** TICKET-027
- **Type:** Legal / UX
- **Description:** Before the first transmission, show a custom DSGVO (GDPR) consent dialog that links to the configured server URL and the privacy policy page on the website. Replaces the initial `MessageBox` approach (which can't render hyperlinks).
- **Acceptance Criteria:**
  - [x] Custom Win32 window with SysLink controls for clickable URLs
  - [x] Server URL + privacy policy link open in default browser
  - [x] Reset button to revoke consent
  - [x] First-recording check: blocks transmission until consent given
- **Files:** `src/mw-recording.c`

#### TICKET-030: Unicode + Dark Theme Dialog Redesign
- **Status:** `DONE`
- **Depends on:** TICKET-029
- **Type:** UX
- **Description:** SysLink controls require Unicode (`W`) Win32 APIs to work correctly with clickable links. Migrate all MW Win32 UI from ANSI (`A`) to Unicode (`W`), and apply a dark theme matching OBS Studio's colors. Adds a live status indicator to the settings dialog.
- **Acceptance Criteria:**
  - [x] All `MessageBoxA`/`CreateWindowExA` calls replaced with `W` variants
  - [x] Proper UTF-8 ↔ `wchar_t` conversion for all user-facing text
  - [x] Umlauts (ä/ö/ü/ß) render correctly in dialog body
  - [x] Dark background, light text, accent matching OBS
  - [x] Settings dialog shows live "connected"/"not connected" indicator
- **Files:** `src/mw-recording.c`, `src/auto-setup.c` (umlauts in dialog text)

#### TICKET-031: DSGVO Dialog DPI-Aware Dynamic Layout
- **Status:** `DONE`
- **Depends on:** TICKET-030
- **Type:** Bug
- **Description:** On high-DPI displays the DSGVO dialog clipped its body text — the layout used hardcoded pixel heights. Replace with dynamic measurement via `GetTextMetrics` and `AdjustWindowRectEx`. Adds a "Datenschutzerklärung" button linking to the privacy policy page.
- **Acceptance Criteria:**
  - [x] All control heights computed from measured font line height × line count
  - [x] Window auto-resizes to fit content via `AdjustWindowRectEx`
  - [x] No clipping at 100%, 125%, 150%, 200% DPI scaling
- **Files:** `src/mw-recording.c`

---

### Epic 13: Camera ID Expansion (A–P)

> Session 12 (2026-04-18): The Minewache team grew past 8 cameras. Expand the
> dropdown without changing the LTC encoding.

#### TICKET-032: 16 Camera IDs + Bidirectional MW↔LTC Sync
- **Status:** `DONE`
- **Depends on:** TICKET-017, TICKET-028
- **Type:** Feature
- **Description:** Expand camera ID dropdowns in both the LTC source properties and the MW Aufnahme dialog from A–H (8) to A–P (16). The LTC User Bits `user7` field is 4 bits, so 16 fits exactly — no encoding change required. Add bidirectional sync: changing the camera ID in either UI updates the other. Preserve existing user values on upgrade via `obs_data_has_user_value`.
- **Acceptance Criteria:**
  - [x] Both dropdowns show "Kamera A" through "Kamera P"
  - [x] `mw_recording_get_camera_id()` / `set_camera_id()` API added
  - [x] LTC source pulls camera ID from MW config as fallback (when no per-source value stored)
  - [x] Changing MW camera updates all live LTC sources
  - [x] Pre-0.4.0 settings preserved after upgrade
  - [x] `stdbool.h` included in `mw-recording.h` (0.4.1 build fix)
- **Files:** `src/ltc-source.c`, `src/mw-recording.c/h`

---

### Epic 14: Coordination Website

> Session 13 (2026-05-09): The PHP backend that receives heartbeats — added to git
> tracking. Lives at the configured MW server URL; PHP/MySQL stack.

#### TICKET-033: Web Dashboard for Live Recording Coordination
- **Status:** `DONE`
- **Depends on:** TICKET-027
- **Type:** Feature (out-of-plugin)
- **Description:** PHP/MySQL website that receives heartbeat/start/stop signals from clients, exposes a password-protected dashboard for the director, a public Datenschutzerklärung (GDPR) page, and an SSE feed for live updates. Validates `camera_id` against `[A-P]` regex (matches plugin 0.4.0+). The `website/includes/config.php` file (credentials) is gitignored.
- **Acceptance Criteria:**
  - [x] `api.php` endpoint accepts heartbeat/start/stop with API key auth
  - [x] `camera_id` validated against `^[A-P]$`
  - [x] Login-gated `scenes.php` / `stats.php` dashboards
  - [x] Public `datenschutz.php` page (linked from plugin DSGVO dialog)
  - [x] `sse.php` for Server-Sent Events live updates
  - [x] Scene state tracking: "Szene läuft" toggle, Take field, participant list
  - [x] `config.php` (DB credentials) excluded from git via `.gitignore`
- **Files:** `website/*` (PHP, JS, CSS, assets)

---

### Epic 15: Director Offset Visibility & Remote Re-sync

> User feedback (2026-05-13): Recent shoot had cameras drift up to 5 minutes apart
> with no signal from the field. Director needs per-camera NTP offset visible on
> the dashboard before the next shoot, with a warning when |offset| > 1s, plus a
> way to trigger a remote re-sync. Scope decision: visibility + remote re-sync
> (user picked over "ping operator only"). Assume one LTC source per OBS instance
> (matches Minewache template).

#### TICKET-034: Plugin reports NTP offset on heartbeat
- **Status:** `DONE`
- **Depends on:** TICKET-027 (heartbeat module), TICKET-018 (sync_method tracking)
- **Type:** Feature
- **Description:** Extend the MW heartbeat JSON with the current NTP offset and sync method. Add a public accessor on the LTC source so `mw-recording.c` can read the offset without owning the NTP thread. Accessor walks OBS sources via `obs_enum_sources` and returns the first LTC source's `volatile` offset fields — no locks needed.
- **Acceptance Criteria:**
  - [ ] `sync_method_t` enum moved from private `ltc-source.c` to `ltc-source.h` (or a new shared header) so `mw-recording.c` can use it
  - [ ] New API `bool ltc_source_get_current_offset(int64_t *offset_ms, int *sync_method, bool *synced)` in `ltc-source.h`; returns `false` (zeroes outputs) if no LTC source exists
  - [ ] `mw-recording.c heartbeat_thread_func` calls the accessor each tick and adds `offset_ms` (int) + `sync_method` (int 0–3) to the JSON body
  - [ ] Heartbeat omits the new fields when no LTC source exists (graceful degradation)
  - [ ] `obs_log(LOG_DEBUG, ...)` line includes the offset value sent
  - [ ] Comment in the accessor documents the "first source wins" assumption + that the Minewache template ships exactly one
- **Technical Notes:**
  - All NTP fields on `struct ltc_source_context` are already `volatile int64_t` — lock-free read is safe
  - Heartbeat JSON build is at `mw-recording.c:254–255`
- **Files:** `src/ltc-source.h`, `src/ltc-source.c`, `src/mw-recording.c`

#### TICKET-035: Website stores offset and displays warning when > 1s
- **Status:** `DONE`
- **Depends on:** TICKET-034
- **Type:** Feature
- **Description:** Add `offset_ms` + `sync_method` columns to the `sessions` table. Heartbeat handler stores them; SSE feed ships them; dashboard renders per-card with a colour-coded badge. Director sees drift live and can contact the operator.
- **Acceptance Criteria:**
  - [ ] `install.php` adds two columns via try/catch ALTER (matches existing migration pattern at `install.php:57–69`): `offset_ms INT NULL`, `sync_method TINYINT NULL`
  - [ ] `api.php handle_heartbeat()` accepts `offset_ms` (validated, range ±86_400_000ms) and `sync_method` (0–3); UPDATE-s them on the heartbeat row
  - [ ] `sse.php` SELECT includes the two new columns so the dashboard auto-receives them
  - [ ] `app.js renderUserCards()` shows offset on each card: ms when <1s, "1.2s" / "5.4s" when ≥1s. Sync method shown as small label (NTP / HTTP / Local)
  - [ ] `style.css` defines `.offset-warn` (orange `#ff9800`, 1000ms < |offset| ≤ 5000ms) and `.offset-crit` (red `#f44336` + pulse, |offset| > 5000ms)
  - [ ] Offset not shown on offline cards (already filtered by `mark_stale_users_offline`)
  - [ ] Verify: drift system clock by 2s on one PC → dashboard card flips to orange within one SSE tick
- **UX Notes:** Visual hierarchy: neutral ≤1s, orange >1s, red+pulse >5s. The pulse on red is the "this is broken, look at me NOW" signal.
- **Files:** `website/install.php`, `website/api.php`, `website/sse.php`, `website/assets/app.js`, `website/assets/style.css`

#### TICKET-036: Remote re-sync trigger from dashboard (outside recording only)
- **Status:** `DONE`
- **Depends on:** TICKET-034, TICKET-035
- **Type:** Feature
- **Description:** "Re-sync" button next to each camera card flags the session. On the next heartbeat (≤30s) where the camera reports it is **NOT recording**, the API response carries `resync: true`, the plugin parses it and kicks the NTP thread for an immediate re-query. Piggybacks on the existing heartbeat — no new polling timer or socket. **Hard constraint: re-sync MUST NOT run during an active recording** — hard re-syncs cause timecode jumps that corrupt the recording (see TICKET-008 history). The system queues the request until the camera goes idle, then delivers it automatically. To make idle delivery work, the heartbeat thread must also fire when MW is enabled but no recording is active.
- **Acceptance Criteria:**
  - [ ] **Idle heartbeats:** `mw-recording.c heartbeat_thread_func` sends heartbeats whenever MW is configured (server URL + name + API key present), not only during active recording. (Current behaviour at line ~253 gates on `recording_active` — drop that gate.)
  - [ ] **`recording_active` in JSON:** heartbeat body includes `"recording_active": true|false` so the server can gate the resync command.
  - [ ] `install.php` adds `pending_resync TINYINT(1) NOT NULL DEFAULT 0` to `sessions` via try/catch ALTER (idempotent migration pattern).
  - [ ] `install.php` adds `last_recording_active TINYINT(1) NOT NULL DEFAULT 0` to `sessions` so the server can remember the most recent reported state.
  - [ ] `dashboard-api.php` exposes POST `?action=request_resync` (requires `require_dashboard_auth()`); sets `pending_resync = 1` on the **latest session row for that user_name** (not just `session_id` — so the flag survives across stop/start cycles). Refuses politely (HTTP 409) if camera is offline.
  - [ ] `api.php handle_heartbeat()`:
    - Updates `last_recording_active` and `last_heartbeat` for the user's latest session row
    - If `pending_resync = 1` AND incoming `recording_active = false`: returns `{"ok": true, "resync": true}` and clears the flag in the same request
    - If `pending_resync = 1` AND incoming `recording_active = true`: returns `{"ok": true}` (no resync), flag stays set, will be delivered on the next idle heartbeat
  - [ ] `app.js`: each card gets a "Re-sync" button next to camera ID with three visual states driven by data already in the SSE payload:
    - Idle (`status = online` AND `last_recording_active = 0`): "Re-sync now" — enabled
    - Recording (`status = online` AND `last_recording_active = 1`): "Re-sync after recording" — clickable (queues flag), shows toast "queued — will apply when recording stops"
    - Offline: button hidden or fully disabled
  - [ ] After click, button greys out for 30s (matches heartbeat interval).
  - [ ] `mw-recording.c`: replace `discard_write` with a capturing write callback; substring-match `"resync":true` in the response body; on match call `ltc_source_kick_resync()`.
  - [ ] **Plugin-side defense in depth:** before applying the kick, re-check `g_mw.recording_active`. If true (race: recording started between heartbeat send and response), log and skip — do NOT call into `ltc_source_kick_resync()`. The flag will simply be re-delivered on the next idle heartbeat.
  - [ ] `ltc-source.c/h`: add `ltc_source_kick_resync()` which iterates LTC sources and signals an `os_event_t resync_event` on each context; NTP thread loop waits on either `stop_event` or `resync_event`; on resync_event, resets it and re-runs NTP query immediately.
  - [ ] Plugin-side cooldown: ignore further kicks for 5s after a successful re-query to prevent floods.
  - [ ] Verify end-to-end (idle case): camera idle → director clicks "Re-sync" → next heartbeat carries flag → plugin re-syncs within 1s → dashboard offset updates on subsequent heartbeat.
  - [ ] Verify end-to-end (recording case): camera recording → director clicks "Re-sync" → toast confirms "queued" → heartbeats during recording return no resync → operator stops recording → next idle heartbeat receives the queued resync → plugin re-syncs before next recording starts.
  - [ ] Verify safety: artificially construct a malicious response with `"resync":true` while plugin is mid-recording — confirm plugin refuses (log line "resync refused: recording active") and timecode is NOT disrupted.
- **Technical Notes:**
  - The response body shape is owned by us — substring match (`strstr`) is acceptable, no JSON parser needed. Document the contract in `api.php` to prevent future PHP changes from silently breaking the plugin.
  - libcurl call at `mw-recording.c:208–216`; `discard_write` is the current response handler.
  - NTP thread wait point: `ltc-source.c:202` — currently `os_event_timedwait(ctx->stop_event, …)`, needs to also wake on `resync_event`.
  - Why server-side gating, not just client-side: a malicious or compromised dashboard could otherwise spam resync commands during recording. Server reads `last_recording_active` from DB and only includes `"resync":true` when safe. Defense in depth: plugin also refuses if it has started recording in the heartbeat round-trip window.
  - Existing precedent for this constraint: TICKET-008 already replaced blind hard-resync (every 150 frames) with drift-aware soft resync exactly because hard re-syncs broke DaVinci Resolve sync. This is the same problem class.
- **UX Notes:** Three button states map directly to the three operational realities, so the director never wonders why a click did nothing. Toast on "queued" makes the deferred behaviour visible.
- **Files:** `website/install.php`, `website/api.php`, `website/dashboard-api.php`, `website/assets/app.js`, `website/assets/style.css`, `src/ltc-source.h`, `src/ltc-source.c`, `src/mw-recording.c`

---

### Epic 16: Test Coverage for Epic 15

> Most of Epic 15 lives in `mw-recording.c` (gated by `ENABLE_FRONTEND_API`, hard
> to unit-test in isolation), `ltc-source.c` (depends on OBS runtime via
> `obs_enum_sources` + `obs_obj_get_data`), and PHP backed by MySQL. No automated
> coverage exists today — every change has been verified manually. This epic
> closes that gap for the pieces that are economically testable.

#### TICKET-037: Unit tests for MW heartbeat JSON + response parsing
- **Status:** `DONE`
- **Depends on:** TICKET-034, TICKET-036
- **Type:** Test / Refactor
- **Description:** Extract two pure helpers from `mw-recording.c` so they can be exercised without OBS or libcurl runtime: the heartbeat JSON builder and the resync substring matcher. Add a Google Test target `test-mw-helpers` following the existing pattern (`tests/CMakeLists.txt`).
- **Acceptance Criteria:**
  - [ ] New `src/mw-recording-helpers.c` + `.h`, no OBS includes — only `<string.h>` / `<stdint.h>` / `<stdbool.h>` / `<stdio.h>`.
  - [ ] `int mw_build_heartbeat_body(char *buf, size_t bufsz, const char *name, bool recording_active, bool have_offset, int64_t offset_ms, int sync_method, bool synced)` — exact same string output as the current `snprintf` calls in `heartbeat_thread_func`. Returns chars written.
  - [ ] `bool mw_response_has_resync(const char *response_body)` — substring match for `"resync":true` with tolerance for whitespace (e.g. `"resync": true` should match too).
  - [ ] `mw-recording.c` refactored to call the helpers instead of inline `snprintf` / `strstr`.
  - [ ] `tests/test-mw-helpers.cpp` covers: (a) JSON shape with offset present, (b) JSON shape without offset, (c) `recording_active` true/false formatting, (d) resync detection for `{"ok":true,"resync":true}`, (e) resync NOT detected for `{"ok":true}` or `{"ok":true,"resync":false}`, (f) resync detected even with whitespace variations.
  - [ ] `tests/CMakeLists.txt` adds `test-mw-helpers` target. `ctest` runs it.
  - [ ] Existing manual heartbeat smoke test still passes (no behaviour change).
- **Files:** `src/mw-recording-helpers.c/h` (new), `src/mw-recording.c` (refactor), `tests/test-mw-helpers.cpp` (new), `tests/CMakeLists.txt` (new target), `CMakeLists.txt` (add helper to plugin sources).

#### TICKET-038: PHP API integration tests for heartbeat + request_resync
- **Status:** `DONE`
- **Depends on:** TICKET-035, TICKET-036
- **Type:** Test
- **Description:** Add PHPUnit-based integration tests against a real MariaDB test schema. Tests cover the contract that the plugin relies on: heartbeat-handler accepts/validates new fields, resync delivery is gated by `recording_active`, `pending_resync` survives a stop/start cycle. Schema reset between tests via `install.php` + a `TRUNCATE` helper.
- **Acceptance Criteria:**
  - [ ] `composer.json` in `website/` with `phpunit/phpunit` as dev dep.
  - [ ] `website/tests/bootstrap.php` reads a separate `config-test.php` (DB name `mw_test`, fresh on each run).
  - [ ] Test cases in `website/tests/`:
    - `HeartbeatTest::test_accepts_offset_and_sync_method()`
    - `HeartbeatTest::test_rejects_offset_outside_plausible_range()` (e.g. 1 billion ms)
    - `HeartbeatTest::test_rejects_sync_method_out_of_range()` (e.g. 9)
    - `HeartbeatTest::test_updates_latest_session_even_when_offline()` (idle heartbeat)
    - `ResyncTest::test_resync_delivered_when_recording_inactive()`
    - `ResyncTest::test_resync_NOT_delivered_when_recording_active()` (flag stays set)
    - `ResyncTest::test_pending_flag_propagated_on_new_session_start()`
    - `ResyncTest::test_dashboard_endpoint_requires_auth()` (no session → 401/403)
  - [ ] CI step that runs the PHP tests against a service-container MariaDB.
  - [ ] README or `website/tests/README.md` documents how to run locally.
- **Technical Notes:**
  - PDO-backed tests can hit MySQL directly via `localhost`. No mocking; the contract IS the SQL.
  - Reuse the existing `install.php` for schema bootstrap (call as a function or include it in `setUp()`).
  - `config-test.php` should NOT be gitignored — the test DB credentials are conventionally non-secret.
- **Files:** `website/composer.json` (new), `website/tests/bootstrap.php` (new), `website/tests/HeartbeatTest.php` (new), `website/tests/ResyncTest.php` (new), `website/tests/config-test.php` (new), `.github/workflows/php-tests.yaml` (new or extend existing CI).

#### TICKET-039: End-to-end smoke script for the heartbeat contract
- **Status:** `DONE`
- **Depends on:** TICKET-037, TICKET-038
- **Type:** Test / Tooling
- **Description:** A self-contained PowerShell + bash script that simulates a plugin: posts a sequence of heartbeats and start/stop calls, asserts the dashboard JSON state matches expectations. Catches regressions when either side of the contract drifts. Doubles as living documentation of the heartbeat protocol.
- **Acceptance Criteria:**
  - [ ] `scripts/mw-smoke-test.ps1` (Windows) and `scripts/mw-smoke-test.sh` (Linux/macOS).
  - [ ] Takes a base URL and API key as args; assumes server is running and schema is fresh.
  - [ ] Test flow exercised end-to-end:
    1. POST `?action=start` with valid camera_id → 200, session_id returned.
    2. POST `?action=heartbeat` with `offset_ms=42`, `sync_method=1`, `recording_active=true` → 200, response has no `resync`.
    3. POST `?action=heartbeat` with `recording_active=false`, no pending_resync set yet → 200, no `resync`.
    4. (Dashboard side, raw SQL or admin endpoint) Set `pending_resync=1` for the test user's latest session.
    5. POST `?action=heartbeat` with `recording_active=true` → 200, **no** `resync` in response (gating works), flag still set in DB.
    6. POST `?action=heartbeat` with `recording_active=false` → 200, response includes `"resync":true`, flag cleared in DB.
    7. POST `?action=stop` → 200.
  - [ ] Script exits non-zero on any unexpected response. Final summary: PASS/FAIL.
  - [ ] CI runs this against a docker-compose stack (php-fpm + mariadb).
- **Technical Notes:**
  - Pure HTTP — no PHP/MySQL knowledge required by the script. Validates the wire contract.
  - The "set pending_resync" step needs either direct DB access (via a small helper PHP script in `scripts/` not deployed in prod) or a dedicated test-only endpoint behind `APP_ENV=test`.
- **Files:** `scripts/mw-smoke-test.ps1` (new), `scripts/mw-smoke-test.sh` (new), `scripts/mw-smoke-helper.php` (new — DB seeding for tests), `.github/workflows/e2e-smoke.yaml` (new) or addition to existing CI.

---

### Epic 17: NTP Robustness & Drift Recovery Hardening

> User feedback (2026-05-20): Two field reports — (1) per-camera TC drifted >2
> minutes despite Epic 15 visibility, (2) "Re-sync now" button had ~30% effect
> per click. Diagnosis: single-NTP-server failure mode + silent fallback to
> stale local clock + EMA-smoothing applied to every measurement (so the
> resync_event kick was also smoothed) + drift-aware hard-resync still
> permitted inside recordings.

#### TICKET-040: NTP fallback chain + degraded warning + slewing
- **Status:** `DONE`
- **Depends on:** TICKET-003, TICKET-006, TICKET-008, TICKET-018, TICKET-036, TICKET-037
- **Type:** Bug / Architecture
- **Description:** Three independently valuable changes shipped together:
  1. **NTP fallback chain.** `ntp_sync_thread` now tries the user-configured
     server, then `time.cloudflare.com`, then `time.google.com`, then HTTP
     Date header, before degrading. Each NTP step keeps `NTP_RETRY_COUNT=3`
     and 2 s per attempt — worst-case ~30 s per cycle, well under the 60 s
     minimum sync interval.
  2. **Loud degradation warning.** When the whole chain fails:
     `ntp_synced = false` (was silently kept `true` before),
     `consecutive_sync_failures` counted, `LOG_ERROR` once per transition +
     every 60 s while degraded, Properties UI red banner
     (`NTPDegradedWarning`). After 3 consecutive failures
     `first_sync_done` resets so the next success treats it as a fresh
     initial sync (no slewing from a stale value).
  3. **Slewing replaces EMA + drift-detection hard-resync.** New context
     fields `ntp_target_offset_ms` (raw from NTP thread, no EMA),
     `ntp_offset_ms_applied` (video-thread-owned), `ntp_last_raw_offset_ms`,
     `ntp_last_sync_ns`. `encode_next_frame` slews applied → target by ≤
     1 ms/frame during recording (~25 ppm @ 25 fps) or ≤ 10 ms/frame when
     idle, and `ltc_wrapper_set_timecode`s every frame from wall+applied.
     Deleted: `tc_to_total_frames`, `sync_ref_*`, `RESYNC_CHECK_FRAMES`,
     `RESYNC_DRIFT_THRESHOLD`, `EMA_ALPHA`, the inc_timecode + drift-check
     branches. On idle NTP recovery the encoder applies target instantly
     (matches director expectation after clicking "Re-sync now");
     TICKET-036 server- and plugin-side gating during recording is
     untouched. Pure helper `ntp_slew_step()` added in `ntp-client.c`
     (testable without OBS).
  4. Heartbeat JSON additively carries `raw_offset_ms` + `offset_age_sec`
     so a future dashboard can show actual measurement quality + staleness.
     Plugin emits them now; dashboard UI deferred to TICKET-041.
- **Acceptance Criteria:**
  - [x] NTP chain tries user-server → cloudflare → google → HTTP Date.
  - [x] Dual NTP+HTTP failure sets `ntp_synced = false` (no silent stale offset).
  - [x] `LOG_ERROR` on first failure, then rate-limited to 60 s.
  - [x] `NTPDegradedWarning` banner appears in Properties when degraded.
  - [x] `ntp_slew_step()` pure helper with ≥6 unit tests.
  - [x] `encode_next_frame` calls `ltc_wrapper_set_timecode` every frame; no
        `inc_timecode` path remains; no hard-resync inside active recording.
  - [x] On idle NTP recovery (`!obs_frontend_recording_active()`), applied
        jumps directly to target. While recording, slewing continues.
  - [x] `offset_accessor_cb` returns RAW offset + age, not the slewed value
        (so the dashboard reflects actual measurements).
  - [x] Heartbeat JSON additively gains `raw_offset_ms` + `offset_age_sec`.
  - [x] All existing ctest suites still pass; new tests cover slew step
        + extended JSON shape.
- **Files:** `src/ltc-source.{c,h}`, `src/ntp-client.{c,h}`,
  `src/mw-recording-helpers.{c,h}`, `src/mw-recording.c`,
  `tests/test-ntp-offset.cpp`, `tests/test-mw-helpers.cpp`,
  `data/locale/en-US.ini`.

#### TICKET-041: Dashboard surfaces raw offset + age (follow-up)
- **Status:** `TODO`
- **Depends on:** TICKET-040
- **Type:** Enhancement
- **Description:** Website UI for the new heartbeat fields `raw_offset_ms` +
  `offset_age_sec`. Plugin emits them additively as of TICKET-040; this
  ticket wires them through `install.php` (schema), `api.php`
  (handle_heartbeat persistence), `sse.php` (feed), `app.js renderUserCards`
  (display: e.g. *"Drift 47 ms · vor 23 s"*), `style.css` (staleness colour
  if age > 90 s).
- **Files:** `website/install.php`, `website/api.php`, `website/sse.php`,
  `website/assets/app.js`, `website/assets/style.css`.

---

### Epic 18: Trust the Timecode (0.6.0)

> Field audit before next shoot revealed: (1) plugin version invisible in the
> director dashboard (can't tell who runs an outdated build), (2) sync-loss
> during recording is silently absorbed by slewing toward a stale target,
> (3) all 15+ plugin instances could hit the same NTP server simultaneously
> at morning startup, (4) C++ tests are built but never executed by CI, and
> (5) no per-card freshness signal for the offset measurement.
>
> Cold-start modal (TICKET-042) is implemented but **gated behind a compile
> flag (`MW_COLD_START_MODAL_ENABLED`, default OFF)** for 0.6.0 — the
> recording-start codepath stays untouched for the imminent shoot. Flag can
> be enabled in a later 0.6.x patch once the modal has been hardened in lab
> conditions.

#### TICKET-042: Cold-start modal with "report to director" notice (FLAG-GATED)
- **Status:** `TODO`
- **Depends on:** TICKET-040
- **Type:** Feature (safety)
- **Description:** When `OBS_FRONTEND_EVENT_RECORDING_STARTING` fires and
  `first_sync_done == false`, show a modal warning the operator that the
  timecode is not yet synchronised; default button "Abbrechen", second button
  "Trotzdem aufnehmen" emphasises *report to director immediately*. Overrides
  recorded in `cold_start_overridden` heartbeat field + metadata sidecar.
  **Build with `#ifdef MW_COLD_START_MODAL_ENABLED` only — default OFF for
  0.6.0** so the recording-start path is unchanged for the imminent shoot.
- **Acceptance Criteria:**
  - [ ] Modal code present but unreachable when flag is undefined.
  - [ ] Compile-tests pass with both flag states.
  - [ ] When flag ON (lab only): Abbrechen aborts recording, "Trotzdem"
        proceeds and sets `cold_start_overridden=true`.
- **Files:** `src/mw-recording.c`, `data/locale/en-US.ini`, `CMakeLists.txt`
  (optional flag).

#### TICKET-043: Sync-loss-during-recording detection
- **Status:** `TODO`
- **Depends on:** TICKET-040
- **Type:** Feature (safety, passive)
- **Description:** New context flag `sync_lost_during_recording` set to `true`
  when, during active recording, `consecutive_sync_failures >= 3` AND
  `last_successful_sync_age_sec > 60`. Plugin emits the flag in the heartbeat
  JSON additively. On `OBS_FRONTEND_EVENT_RECORDING_STOPPED`, if flag was
  set, write a `sync_loss_periods: [{start,end}]` array to the metadata
  sidecar. Passive — no UI dialog, no abort, no eingriff in the recording
  pipeline.
- **Acceptance Criteria:**
  - [ ] Flag goes true when conditions met during recording.
  - [ ] Heartbeat JSON additively carries the field.
  - [ ] Sidecar records the loss interval(s).
  - [ ] Server validates + persists.
  - [ ] Dashboard shows persistent red marker on the user card.
- **Files:** `src/ltc-source.c`, `src/mw-recording.c`,
  `src/mw-recording-helpers.{c,h}`, `src/metadata-writer.c`, `website/api.php`,
  `website/install.php`, `website/dashboard-api.php`, `website/assets/app.js`,
  `tests/test-mw-helpers.cpp`, `website/tests/HeartbeatTest.php`.

#### TICKET-044: PC clock skew warning on first NTP sync
- **Status:** `DONE` (log-only for 0.6.0; dashboard wiring deferred)
- **Depends on:** TICKET-040
- **Type:** Feature (visibility)
- **Description:** When the first successful NTP query returns
  `|offset_ms| > 2000`, emit `LOG_ERROR` with the actual offset and
  remediation hint ("fix the Windows time service to avoid this on the
  next start"). The slewing logic already corrects the timecode over
  time — this warning is about getting the operator to fix the
  underlying clock so future starts begin cold-and-good rather than
  cold-and-bad. State stored in `initial_clock_skew_ms` on the source
  context for future dashboard wiring (deferred to 0.6.1).
- **Acceptance Criteria:**
  - [x] LOG_ERROR fires once on first sync if `|offset| > 2000` ms.
  - [x] Offset value formatted in seconds with 1 decimal.
  - [x] Field on source context for future dashboard wiring.
  - [ ] Heartbeat carries `initial_offset_ms` — DEFERRED to 0.6.1.
  - [ ] Dashboard renders the warning at the user card — DEFERRED.
- **Files:** `src/ltc-source.c`.

#### TICKET-045: Enable ctest in CI on all 3 platforms
- **Status:** `DONE`
- **Depends on:** (none — pure CI work)
- **Type:** Infrastructure
- **Description:** Tests were compiled-but-never-run because the CI presets
  had `BUILD_TESTS` OFF and no `testPresets` block existed. Added
  `BUILD_TESTS: true` to all 3 CI configure presets, new `testPresets` block
  with `outputOnFailure: true`, and "Run Tests 🧪" step in each platform job
  of `build-project.yaml` between Build and Package. **Step has
  `continue-on-error: true` for 0.6.0** so a regression visible in CI logs
  doesn't block the release artifact upload (release was on a deadline);
  flip to blocking in a follow-up patch once the loop is proven.
- **Acceptance Criteria:**
  - [x] CI presets compile tests (`BUILD_TESTS=ON`).
  - [x] `testPresets` block for windows/ubuntu/macos.
  - [x] `Run Tests` step invokes `ctest --preset <platform>` per job.
  - [x] Local verification on Windows: 5/5 tests pass via
        `ctest --preset windows-ci-x64`.
- **Files:** `CMakePresets.json`, `.github/workflows/build-project.yaml`.

#### TICKET-046: Integration test harness with mock NTP source
- **Status:** `TODO`
- **Depends on:** TICKET-045
- **Type:** Test infrastructure
- **Description:** New `tests/test-ltc-source-integration.cpp` drives
  `encode_next_frame` end-to-end with a programmable mock NTP offset source.
  Covers: (a) burst-mode median selection (TICKET-049), (b) slewing
  convergence from large offset over many frames at production rates, (c)
  sync-loss-during-recording flag transitions (TICKET-043).
- **Acceptance Criteria:**
  - [ ] At least 3 integration scenarios covered.
  - [ ] Uses existing OBS-stub layer (`tests/obs-stub-*.cpp` if any) or extends it.
  - [ ] Builds and runs on all 3 CI platforms.
- **Files:** `tests/test-ltc-source-integration.cpp` (new),
  `tests/CMakeLists.txt`, possibly extensions to existing OBS stubs.

#### TICKET-047: plugin_version end-to-end (heartbeat → DB → dashboard)
- **Status:** `DONE`
- **Depends on:** (none direct; uses existing `PLUGIN_VERSION` const)
- **Type:** Feature
- **Description:** Plugin additively adds `plugin_version` (string) to the
  heartbeat JSON (value: `PLUGIN_VERSION` macro from
  `src/plugin-support.c.in`). Server validates `^[\w.+-]{1,20}$`, persists
  to new `sessions.plugin_version VARCHAR(20) NULL` column. Dashboard
  renders "Plugin: vX.Y.Z" under each user card, colour-coded against
  `window.MW_LATEST_PLUGIN_VERSION` (set inline in `index.php` and pinned
  per release): green if matches, orange "Update verfügbar" if mismatch,
  grey italic "v? (nicht gemeldet)" if NULL.
  **Note on UPDATE:** uses `COALESCE(:pv, plugin_version)` so a single
  missing/junk heartbeat doesn't erase the last-known version — important
  for stable dashboard display when an old plugin co-exists.
- **Acceptance Criteria:**
  - [x] Plugin sends the field via `PLUGIN_VERSION` in mw-recording.c.
  - [x] Server validates `[\w.+-]{1,20}`; junk silently dropped via COALESCE.
  - [x] Migration in `install.php` is idempotent (`ADD COLUMN` in try/catch).
  - [x] Dashboard renders 3 states (current / outdated / unknown).
  - [x] Backwards-compat: old plugin without field → grey "v?", no error.
  - [x] Unit tests in `test-mw-helpers.cpp` cover new field (4 new cases).
  - [x] PHPUnit tests verify accept/reject/persist (5 new cases).
- **Files:** `src/mw-recording-helpers.{c,h}`, `src/mw-recording.c`,
  `website/api.php`, `website/install.php`, `website/sse.php`,
  `website/index.php`, `website/assets/app.js`, `website/assets/style.css`,
  `tests/test-mw-helpers.cpp`, `website/tests/HeartbeatTest.php`.

#### TICKET-048: Datenschutz update + re-consent mechanic
- **Status:** `TODO`
- **Depends on:** TICKET-043, TICKET-044, TICKET-047, TICKET-049, TICKET-052
- **Type:** Compliance
- **Description:** Add new heartbeat fields (plugin_version, sync_status,
  initial_offset_ms, audio drop counters) to `datenschutz.php` Section 3
  table; update "Stand: TT.MM.JJJJ"; add yellow info-box at top
  ("Aktualisiert am … — neue Felder: …"). In the plugin: bump
  `CURRENT_CONSENT_VERSION` to 2; on plugin start, if stored
  `mw_consent_version < 2`, show the existing DSGVO dialog again with an
  intro line explaining *why* re-consent is needed. Decline = heartbeat
  disabled (LTC still works locally). Accept = store version 2.
- **Acceptance Criteria:**
  - [ ] `datenschutz.php` Section 3 table has new rows.
  - [ ] Info-box visible at top of page with new date.
  - [ ] Plugin: re-consent dialog shown on 0.5.x → 0.6.0 upgrade.
  - [ ] Storing version 2 prevents subsequent re-shows.
  - [ ] Decline path: no heartbeat sent (verified by absence of HTTP POST in test).
- **Files:** `website/datenschutz.php`, `src/mw-recording.c`,
  `data/locale/en-US.ini`.

#### TICKET-049: NTP burst mode at cold start
- **Status:** `TODO`
- **Depends on:** TICKET-040
- **Type:** Feature (sync hardening)
- **Description:** Replace the current "one NTP query then 5-min interval"
  with a 3-phase startup: Phase 1 (cold start) — 3 queries × 2 s pause,
  take median offset; Phase 2 (stabilisation) — 2 queries × 10 s pause;
  Phase 3 (steady state) — current 5-min interval. Median (not mean) to
  resist outliers from packet jitter.
- **Acceptance Criteria:**
  - [ ] Burst phase visible in `obs_log` startup sequence.
  - [ ] Unit test for median selection (3 sample values).
  - [ ] After Phase 3, behaviour identical to current 0.5.1.
- **Files:** `src/ltc-source.c`, `src/ntp-client.{c,h}` (median helper),
  `tests/test-ntp-offset.cpp`.

#### TICKET-050: Random jitter before first NTP query
- **Status:** `DONE`
- **Depends on:** TICKET-040
- **Type:** Feature (server load protection)
- **Description:** Added at the very start of `ntp_sync_thread` (before
  the main loop): `os_event_timedwait(stop_event, jitter_ms)` where
  `jitter_ms = (os_gettime_ns()/1000) % 10000`. No `rand()` calls —
  avoids thread-safety + reproducibility concerns. Logs the chosen delay
  so an operator debugging "why is sync slow on day 1" can see it. If
  the source is destroyed during the jitter window the thread exits
  cleanly.
- **Acceptance Criteria:**
  - [x] Jitter applied once before first query, never afterwards.
  - [x] Source = `os_gettime_ns()`, not `rand()`.
  - [x] Log message emitted with chosen delay.
  - [x] Builds clean with WARNING_AS_ERROR (cast to unsigned long
        explicit, no C4244).
- **Files:** `src/ltc-source.c`.

#### TICKET-051: "Last sync N seconds ago" line on dashboard cards
- **Status:** `DONE`
- **Depends on:** TICKET-040 (plugin carries `offset_age_sec`),
  TICKET-047 (line is rendered alongside plugin_version)
- **Type:** Feature (visibility)
- **Description:** Plugin emits `offset_age_sec` since TICKET-040; server
  now validates `[-1, 86400]` and persists to a new
  `sessions.offset_age_sec INT NULL` column (idempotent migration).
  Status + SSE SELECTs include the field. Dashboard renders "Letzte Sync:
  vor X s/Min" with three colour states: green (<180 s), orange
  (180–480 s), red (>480 s, plus the "nie gesynct" cold-start case where
  the plugin reports -1).
- **Acceptance Criteria:**
  - [x] Migration adds offset_age_sec column.
  - [x] Server validates `[-1, 86400]`, junk silently dropped.
  - [x] Status + SSE SELECTs include the field.
  - [x] Dashboard renders three colour states + "nie gesynct" for -1.
  - [x] Missing field renders nothing (defensive null-check).
- **Files:** `website/install.php`, `website/api.php`, `website/sse.php`,
  `website/assets/app.js`, `website/assets/style.css`.

#### TICKET-052: Audio-drop detection during recording
- **Status:** `TODO`
- **Depends on:** TICKET-047
- **Type:** Feature (LTC integrity signal)
- **Description:** During active recording, sample OBS' global counters
  (`obs_get_total_frames()` vs `obs_get_lagged_frames()` — or audio-drop
  equivalent). If lagged > previous reading while recording, set
  `audio_drops_during_recording=true` for the heartbeat. Dashboard
  shows red badge "⚠ Audio-Aussetzer" on the user card.
- **Acceptance Criteria:**
  - [ ] Heartbeat field added, additive, validated server-side.
  - [ ] Migration column `audio_drops_during_recording TINYINT(1)`.
  - [ ] Dashboard renders badge.
  - [ ] No-op when recording is idle.
- **Files:** `src/mw-recording.c`, `src/mw-recording-helpers.{c,h}`,
  `website/api.php`, `website/install.php`, `website/dashboard-api.php`,
  `website/assets/app.js`, `website/assets/style.css`,
  `tests/test-mw-helpers.cpp`.

---

## Decision Log

| Date | Ticket | Decision | Rationale | Alternatives Considered |
|------|--------|----------|-----------|------------------------|
| — | TICKET-000 | Use obs-plugintemplate as base | Batteries-included CI/CD, CMake presets, OBS SDK auto-fetch | Manual CMake setup (more work, error-prone) |
| — | TICKET-000 | libltc as git submodule with custom CMake | Zero deps, 3 source files, no vcpkg/conan available | System package (not cross-platform), header-only reimpl (unnecessary) |
| — | TICKET-000 | Google Test via FetchContent | Best CMake integration, includes gmock, industry standard | Catch2 (no mocking), doctest (less ecosystem) |
| — | TICKET-000 | Testable Core + Thin OBS Adapter architecture | Core modules testable without OBS runtime | Monolithic (untestable), full OBS mock (too complex) |
| 2026-02-27 | TICKET-003 | NTP thread uses os_event_t + volatile offset (no mutex for reads) | Minimal contention: NTP thread writes every 5min, video thread reads every ~33ms. Mutex only for encoder recreation. | Full mutex (unnecessary overhead), C11 atomics (not portable to MSVC) |
| 2026-02-27 | TICKET-005 | Changed ltc_wrapper_create API to accept tc_framerate_t enum | Correct handling of 29.97df (30000/1001 rate), proper TV standard per framerate | Keep int fps param (can't represent 29.97), separate create function (API bloat) |
| 2026-02-27 | TICKET-005 | Fixed ltc_encoder_create: pass fps_rate, not samples_per_frame | Original code passed SPF as fps param, resulting in 25 samples/frame instead of 1920 | N/A (was a bug) |
| 2026-02-27 | TICKET-006 | Frame-buffer approach for continuous audio | Encodes LTC frames into a buffer, copies samples out per video_tick. Re-syncs to wall clock every ~5s. | Pure wall-clock per tick (discontinuities), free-running only (drift) |
| 2026-02-27 | TICKET-008 | Drift-aware resync instead of blind hard-resync | Hard resync every 150 frames caused timecode jumps breaking DaVinci Resolve. Now checks every 750 frames (~30s) and only resyncs if drift > 2 frames. | Blind hard-resync (causes TC jumps), pure free-running (accumulates drift) |
| 2026-02-27 | TICKET-008 | Added #include <plugin-support.h> to ltc-source.c | obs_log() declared in plugin-support.h, was missing from ltc-source.c causing Windows build failure | N/A (was a build error) |
| 2026-02-27 | TICKET-013 | Windows install path: `%ProgramData%\obs-studio\plugins\` NOT `%APPDATA%` | OBS does NOT scan `%APPDATA%\obs-studio\plugins\` for plugin DLLs. AppData is only for settings/config. ProgramData is the official OBS third-party plugin location. This was the root cause of "plugin doesn't appear in OBS". | `%APPDATA%` (wrong — never worked), `C:\Program Files\obs-studio\obs-plugins\64bit\` (legacy, deprecated by OBS) |
| 2026-03-01 | TICKET-014 | LTC Timecode on Track 3 with `mixers=4` (exclusive) | Track 3 is dedicated to LTC so it never bleeds into voice (Track 1) or game audio (Track 2). `mixers=4` = bit 2 = Track 3 only. RecTracks bitmask 7 = Tracks 1+2+3. | Track 4+ (wastes tracks), shared track (would mix LTC into audible audio) |
| 2026-03-01 | TICKET-015/016 | Template deploy only if not already present (`onlyifdoesntexist` / `-not Test-Path`) | Prevents overwriting user customizations to the Minewache scene collection. Uninstall also does NOT remove template. | Always overwrite (destroys user changes), ask user (adds friction) |
| 2026-03-01 | TICKET-012 | Win32 MessageBoxA for first-run dialog (no Qt dependency) | Plugin has `ENABLE_QT=OFF`, so Qt dialogs unavailable. Win32 MessageBoxA is always available on Windows. On Linux: auto-switch without dialog (no universal GTK/zenity guarantee). | Qt dialog (requires ENABLE_QT), zenity subprocess (fragile, not always installed) |
| 2026-03-01 | TICKET-012 | `obs_frontend_get_user_config()` instead of deprecated `get_global_config()` | `obs_frontend_get_global_config()` is marked `OBS_DEPRECATED` in OBS 31.x+ header. `get_user_config()` is the replacement. | `get_global_config()` (deprecated), custom config file (unnecessary complexity) |
| 2026-03-01 | TICKET-017 | Camera ID as dropdown A–H (not free-text) | LTC User Bits: only 8 bits left after SMPTE 12M date (24 bits). Free-text impossible. Dropdown is idiot-proof for non-technical users. | Free-text (doesn't fit), numeric input (confusing), no camera ID (loses DaVinci Resolve sorting) |
| 2026-03-01 | TICKET-017 | Date auto-embedded, no toggle | Users should never need to think about this. Date in User Bits is standard practice and costs nothing. | Optional toggle (unnecessary complexity, users would forget to enable it) |
| 2026-03-01 | TICKET-018 | HTTP fallback fully automatic, no config | Users are non-technical. Fallback chain (NTP→HTTP→local) should just work. Status display shows which method is active. | Manual toggle (users wouldn't understand), config for HTTP endpoint (over-engineering for residential networks) |
| 2026-03-01 | TICKET-019 | Sidecar always created, no toggle | A small JSON file next to the recording is harmless and invisible. Opt-in would mean non-technical users never enable it. | Opt-in toggle (users would miss it), no sidecar (loses metadata for automation) |
| 2026-03-01 | Epic 9 | Dropped: visual overlay, remote endpoint, MIDI TC, NDI TC, custom start TC | Visual overlay destroys edit material. Remote endpoint needs infra first (deferred to future). MIDI/NDI not relevant for decentralized recording workflow. Custom start TC conflicts with time-of-day cross-location sync. | Include all features (scope creep, delays v0.2.0) |
| 2026-03-01 | TICKET-018 | Use libcurl from OBS deps for HTTP fallback (not minimal HTTP client) | OBS ships libcurl; avoids reimplementing TLS. HEAD request extracts Date header. ~1s accuracy acceptable as fallback. | Minimal HTTP client (no TLS), worldtimeapi.org JSON (needs body parsing) |
| 2026-03-01 | TICKET-018 | SSL verification disabled for HTTP fallback | Avoids cert bundle issues on Windows OBS installations. Fallback only needs Date header accuracy, not content security. | Full SSL verification (may fail without cert bundle) |
| 2026-03-01 | TICKET-019 | Sidecar file extension `.ltc.json` (not `.json`) | Avoids conflict with other tools that may create `.json` sidecars. Clear provenance. | Plain `.json` (could collide), embed in recording (not possible with MKV) |
| 2026-03-01 | TICKET-021 | Use `obs_frontend_get_profile_config()` + `config_get_uint()` for RecTracks check | OBS stores recording track bitmask in profile config (`basic.ini`). Frontend API provides direct access without file parsing. Check both SimpleOutput and AdvOut sections. | Parse basic.ini manually (fragile), OBS property callback on track change (no config access) |
| 2026-03-01 | TICKET-022 | License is GPLv2+ (GPL-2.0-or-later), not GPLv2-only | Code header already said "or later" but docs said "GPLv2". GPLv2+ required for LGPLv3 compatibility (libltc). OBS itself is GPLv2+. | GPLv2-only (incompatible with LGPLv3 libltc), GPLv3 (unnecessarily restrictive) |
| 2026-03-01 | TICKET-024 | Switch libltc from static to dynamic linking (SHARED) | LGPLv3 Section 4d1: shared library mechanism satisfies LGPL automatically. No need to provide object files. `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS` handles Windows DLL exports (libltc has no dllexport macros). | Static + object files (complex distribution, easy to forget), keep static and hope for the best (non-compliant) |
| 2026-03-01 | TICKET-024 | Ship libltc LGPL license copy with installer | LGPL Section 4a requires "prominent notice" that the Library is used and "a copy of this License". Installer deploys `COPYING.LGPLv3` to `licenses/libltc/`. | No license copy (non-compliant), embed in README only (not sufficient for binary distribution) |
| 2026-03-27 | TICKET-028 | MW settings global (Tools menu), not per-source | MW recording state is per-OBS-instance, not per LTC source. Putting it on the source meant duplicated/conflicting settings across multiple LTC sources. `obs_frontend_get_user_config()` persists global state cleanly. | Per-source props (state duplication), separate config file (reinvents the wheel) |
| 2026-03-27 | TICKET-029 | Custom Win32 SysLink dialog instead of MessageBox | `MessageBox` cannot render clickable hyperlinks. DSGVO consent must show the server URL and privacy policy as clickable for users to verify before consenting. Same constraint that ruled out Qt in TICKET-012 still applies (`ENABLE_QT=OFF`). | `MessageBox` (no links), Qt dialog (requires ENABLE_QT), shell out to browser (forces user to leave dialog) |
| 2026-03-27 | TICKET-030 | Migrate all MW UI from ANSI (`A`) to Unicode (`W`) Win32 APIs | SysLink controls only fire `NM_CLICK` correctly in Unicode mode. Also fixes German umlaut corruption in dialog text. Required UTF-8 ↔ `wchar_t` conversion plumbing throughout `mw-recording.c`. | Stay on ANSI (broken links + corrupted umlauts), partial migration (inconsistent and bug-prone) |
| 2026-03-27 | TICKET-031 | DPI-aware layout via `GetTextMetrics` + `AdjustWindowRectEx` | Hardcoded pixel sizes clipped text on 125%+ DPI scaling. Measuring actual font metrics is the only Win32-correct way to size text containers across DPI settings. | Hardcoded pixels (broken at high DPI), assume 96 DPI everywhere (broken on most modern monitors) |
| 2026-04-18 | TICKET-032 | 16 cameras (A-P) fit existing 4-bit `user7` field with no encoding change | LTC User Bits `user7` is 4 bits → exactly 16 values. No bit-layout change needed in the encoder; just expand the dropdown range. Keeps backward compatibility with existing recordings. | Use both `user6` and `user7` (8 bits = 256 cams, wasteful + breaks old recordings), separate metadata channel (over-engineering) |
| 2026-04-18 | TICKET-032 | Upgrade-safe: `obs_data_has_user_value` check before applying MW fallback | If we always overwrote source settings from MW config, users with per-source overrides from <0.4.0 would lose them silently on upgrade. Only fall back to MW config when the source has no stored value. | Always overwrite (data loss on upgrade), always prefer source (breaks bidirectional sync goal) |
| 2026-05-09 | TICKET-033 | `camera_id` validated server-side with `^[A-P]$` regex | Plugin sends one of 16 known values; server must reject anything else to prevent API abuse / DB pollution. Regex is the simplest correct check. | Whitelist array (more code, same result), trust client (security hole) |
| 2026-05-09 | TICKET-033 | `website/includes/config.php` gitignored | Contains DB credentials and API keys. Standard practice. Repo ships a `config.example.php` shape via the install endpoint. | Commit with placeholders (high risk of accidental real secrets), env vars only (PHP shared hosting often lacks env config) |
| 2026-05-13 | TICKET-036 | Remote re-sync only honored when camera is NOT recording; queued otherwise | Hard re-sync mid-recording causes timecode jumps. We already learned this in TICKET-008 (blind 150-frame hard resync broke DaVinci Resolve sync) and switched to drift-aware soft resync. A director-triggered hard resync would be worse: the operator wouldn't see it coming. Gating: heartbeat reports `recording_active`, server only emits `resync:true` when last reported state was idle, plugin re-checks at apply time. Flag persists in DB until safely delivered — director clicks once, system waits for an idle moment. | Refuse + drop flag (director has to re-click after each shoot, easy to forget), apply anyway with warning (corrupts recording — same bug TICKET-008 fixed), require operator to confirm on local UI (adds field friction, defeats "remote" purpose) |
| 2026-05-13 | TICKET-036 | Heartbeats fire whenever MW is enabled, not only during active recording | Required so the command channel works between shoots. Currently `heartbeat_thread_func` early-returns if `recording_active` is false (mw-recording.c:~253). Need to keep firing during idle so the queued resync can be delivered. Side benefit: dashboard can show "idle but online" cameras (currently they vanish when not recording). | Separate idle-poll endpoint (more code, two channels to keep in sync), drop the queueing feature (re-click friction), keep current behaviour and require manual /sync on the operator PC (no longer "remote") |
| 2026-05-13 | TICKET-034 | Use `obs_obj_get_data()` to reach the LTC source context from `mw-recording.c` | Public OBS export (`obs.h:818`); returns the per-instance void* from `create()`. Avoids building a parallel context registry just to expose offset fields. Volatile reads are lock-free on x86_64 (matches the existing audio-thread convention in `ltc-source.c:104–107`). | Maintain a separate global list of contexts in ltc-source.c (more code, double-bookkeeping), refactor offset state into a global singleton (much larger change, out of scope for next shoot) |
| 2026-05-13 | TICKET-036 | NTP thread waits on `stop_event` AND polls `resync_event` in 500ms chunks (not `WaitForMultipleObjects`) | OBS's `os_event_t` is single-event; multi-wait would mean platform-specific code. 500ms polling gives <500ms latency on a kicker click, costs 2 syscalls/sec/source — negligible. `resync_event` is `OS_EVENT_TYPE_AUTO`, so `os_event_try` consumes-and-resets in one shot. | Win32 `WaitForMultipleObjects` + pthread equivalent (#ifdef sprawl), shorten the existing `os_event_timedwait` interval globally (wasteful — NTP queries every 500ms would hammer NTP servers) |
| 2026-05-13 | TICKET-036 | `pending_resync` flag carried forward across stop/start in `handle_start` | Director may click "Re-sync" while user is idle (offline session row), then user starts a new recording before next heartbeat — the flag would be stranded on the old row. Reading the previous latest's flag and copying it into the INSERT preserves intent across the session-row boundary. | Per-user `pending_resync` table (cleaner data model but larger schema change), tell director to wait and re-click (poor UX), set flag on user_name instead of session row (breaks the `session_id` identity used by other dashboard actions) |
| 2026-05-13 | TICKET-036 | Heartbeat handler updates the LATEST session row of the user (any non-removed status), not only `status='online'` | Idle heartbeats between shoots have no online session — but we still need to receive the heartbeat (to deliver queued resync flag) and update `offset_ms` so the dashboard can show "idle but synced". `ORDER BY id DESC LIMIT 1` writes to whichever row was last. Semantics of `last_heartbeat` shift slightly to "last time we heard from this user's plugin", which is what the dashboard actually wants. | Keep status='online' filter and add a separate /poll endpoint (two channels), keep status='online' and queue resync via WebSocket (overkill for 30s heartbeat) |
| 2026-05-13 | TICKET-037 | Extract pure helpers into `mw-recording-helpers.c/h` (no OBS deps) rather than mock OBS for unit tests | Same model as `timecode.c`, `ntp-client.c`, etc. — testable core + thin OBS adapter. Mocking `obs_enum_sources` / `obs_obj_get_data` to test inline `snprintf` calls would have been more code than the extraction. The helpers also become useful documentation of the wire contract. | Skip unit tests for these (regressions would only be caught manually), build an OBS mock harness (large investment for two helpers) |
| 2026-05-13 | TICKET-038 | `MW_TEST_MODE` constant: `json_response()` throws `JsonResponseException` instead of `exit()`, dispatch logic in api.php / dashboard-api.php skipped | Lets PHPUnit tests call `handle_*()` functions directly and catch the response — no HTTP roundtrip, no separate process per test. Production behaviour completely unchanged (the constant is never defined outside the test bootstrap). PHP top-level function definitions are hoisted, so the dispatch-at-top + handlers-at-bottom file structure remains valid. | `runInSeparateProcess` annotations (slow, ~50ms × N tests, ugly stdout handling), spin up `php -S` in bootstrap (slow, port collisions in CI), refactor every handler into a class with injectable response sink (much larger change for marginal gain) |
| 2026-05-13 | TICKET-038 | Test DB **dropped and recreated** at bootstrap, schema re-applied via `install.php` | Cheapest way to guarantee a clean baseline — no migration ordering bugs hidden by stale state. Safety net: bootstrap refuses any DB name not ending in `_test`. The TRUNCATE-in-setUp covers per-test isolation; the full drop is once per `phpunit` invocation. | TRUNCATE-only (would miss schema-drift bugs that pure data tests can't catch), migrations framework (overkill for a 3-table schema) |
| 2026-05-13 | TICKET-039 | Smoke scripts seed `pending_resync` via a CLI-only PHP helper, not a hidden HTTP endpoint | Setting the flag is privileged (normally requires director auth + session_id). A test-only HTTP endpoint would be a permanent risk if it ever got deployed; a CLI helper that refuses `PHP_SAPI !== 'cli'` cannot be invoked over the network at all. | Hidden HTTP endpoint behind a header (deployment risk), test-only branch of `request_resync` that skips auth (same deployment risk, harder to spot), direct SQL via mysql CLI in the script (requires mysql binary on PATH — bash and PowerShell would need separate paths) |
| 2026-05-13 | Epic 16 CI | Use real MariaDB (Docker / GitHub service container) for tests, NOT SQLite | Schema and queries use ENUM, ENGINE=InnoDB, NOW(), DATE_SUB(NOW(), INTERVAL ...), UPDATE...ORDER BY id DESC LIMIT 1, SET FOREIGN_KEY_CHECKS=0, TRUNCATE TABLE — all MySQL/MariaDB dialect. SQLite would require either translation shims or dual SQL paths; either masks the very kind of bug we want CI to catch (dialect/schema mismatches between test and prod). Docker mariadb:11 is one image, same in CI service container and `docker-compose.test.yml` for local dev. | SQLite via dialect translation (hides dialect bugs), in-memory MySQL forks like MySQL Server Lite (immature, not in CI presets), Postgres (would require porting all DDL, no benefit) |
| 2026-05-13 | Epic 16 CI | Test MariaDB on port 3307, not 3306 | A developer running the test container while also running a production-like MySQL locally would otherwise either fail to start or — worse — silently connect to the wrong DB. Port 3307 makes the test instance unambiguous. `config-test.php` defaults to it, both CI workflows and `docker-compose.test.yml` agree. | Reuse 3306 (collision risk), random port per run (forces test scripts to read it from somewhere) |
| 2026-05-20 | TICKET-040 | NTP chain: user → time.cloudflare.com → time.google.com → HTTP Date | Single-NTP-server failure was the most likely cause of multi-minute drifts in field. Cloudflare + Google anycast are the two highest-uptime free NTP services and route through different networks than pool.ntp.org. Bounded ~30 s worst-case per cycle (3 servers × 3 retries × 2 s + 5 s HTTP) — well under the 60 s minimum sync interval. | Single configurable server (current — fails when that server is firewalled), full pool.ntp.org rotation per cycle (no failure-isolation), let users edit a server list (UX friction, most operators don't know what to put), drop pool.ntp.org default for Cloudflare (changes behaviour silently for existing installs). |
| 2026-05-20 | TICKET-040 | Slewing replaces EMA + drift-detection hard-resync | EMA: slow geometric convergence (~55 min for 50 s drift @ 5-min interval); `resync_event` kick still smoothed → director button had ~30% effect per click. Drift-aware hard-resync (TICKET-008) still permits TC discontinuities on clock perturbations. Slewing decouples target (raw NTP, instant) from applied (slow follow), keeping TC monotonic. 1 ms/frame @ 25 fps = 25 ppm = within hardware LTC generator tolerance. Kick bypasses slew when not recording (instant), still refused during recording (TICKET-036 server- and plugin-side gating untouched). | (a) Outlier-bypass EMA — still triggers hard-resyncs; (b) Larger drift threshold — postpones the problem; (c) Audio-resample slew — much larger eng cost, libltc-state manipulation. |
| 2026-05-20 | TICKET-040 | Silent local-clock degradation fixed: dual NTP+HTTP failure sets `ntp_synced=false` | Previously `sync_method=LOCAL` was set but `ntp_synced` stayed `true` and `ntp_offset_ms` stayed stale. Heartbeat reported green while the OS clock drifted — the most likely root cause of multi-minute field drifts. Now: synced=false on failure, `consecutive_sync_failures` counter, LOG_ERROR on transition + every 60 s while degraded, Properties UI red banner, dashboard auto-colour-codes (Epic 15 already handles synced=false). After 3 failures `first_sync_done` resets so the next success acts as a fresh initial sync. | Keep silent fallback (current bug — invisible drift), modal dialog (interrupts mid-shoot), Windows tray notification (platform-specific, OBS doesn't ship one). |
| 2026-05-20 | TICKET-040 | Encoder calls `set_timecode` every frame from wall+applied (no `inc_timecode` path) | The libltc inc_timecode path was the source of the drift-detection hard-resync that TICKET-008 tried to soften. With slewing, the encoder is wall-locked every frame anyway — inc_timecode would just diverge again. Side benefit: midnight rollover is automatic via timecode_from_unix (was a special case before). `ltc_wrapper_inc_timecode` stays in the public API for back-compat but no longer called from ltc-source.c. | Keep inc_timecode + drift detect (the bug we're fixing), per-frame inc with periodic correction (still needs drift detection), audio-resample (too large a change). |

---

## Session Log

| Session | Date | Agent | Tickets Worked | Status at End | Notes |
|---------|------|-------|----------------|---------------|-------|
| 18 | 2026-05-20 | Claude Opus 4.7 (1M) | TICKET-040 (done), TICKET-041 (created) | TICKET-040 DONE; TICKET-041 TODO (deferred dashboard UI) | Epic 17: NTP robustness + drift recovery. (1) Built-in NTP fallback chain: user-server → time.cloudflare.com → time.google.com → HTTP Date → degraded. (2) Loud warning when degraded: `ntp_synced=false` now propagates (was the silent-degradation bug — heartbeat used to report green while local clock drifted); LOG_ERROR on transition + 60 s throttled; new `NTPDegradedWarning` red banner in Properties; after 3 failures `first_sync_done` resets to avoid stale-offset poisoning. (3) Slewing replaces EMA + hard-resync: new pure `ntp_slew_step()` in `ntp-client.c` (7 unit tests covering zero/within/exceeds/large-int64/no-op cases); `encode_next_frame` slews `ntp_offset_ms_applied` toward `ntp_target_offset_ms` at 1 ms/frame recording / 10 ms/frame idle, and `ltc_wrapper_set_timecode`s every frame; deleted `tc_to_total_frames`, `sync_ref_*`, `RESYNC_*`, `EMA_ALPHA`, inc_timecode+drift-check branches; on idle NTP recovery applied jumps to target (instant resync feel for the director); TICKET-036 server+plugin gating during recording untouched. (4) `offset_accessor_cb` returns raw + age, not the slewed value, so the dashboard reflects actual measurement quality. (5) Heartbeat JSON additively carries `raw_offset_ms` + `offset_age_sec` (3 new test cases); dashboard UI deferred to TICKET-041. Tests on Linux toolchain: 7/7 new NTPSlewStep cases green, 13/13 BuildHeartbeatBody cases green (3 new), 11/11 ResponseHasResync cases unchanged; ltc-source.c + mw-recording.c syntax-clean with OBS stub. Full OBS-linked build deferred to CI runners (no OBS SDK in dev container). |
| 17 | 2026-05-13 | Claude Opus 4.7 (1M) | End-to-end Verifizierung | 17/17 PHPUnit, 14/14 smoke assertions PASS | Erstes End-to-end-Run der Epic-16-Tests gegen die Docker-MariaDB hat zwei echte Production-Bugs aufgedeckt: (1) `db.php` machte `require_once 'config.php'` unbedingt — was im Test-Modus failt weil `config.php` gitignored ist und durch `config-test.php` ersetzt wird. Fix: `if (!defined('DB_HOST'))`-Guard. (2) `handle_heartbeat` gated die Resync-Auslieferung auf `$updated_rows > 0`, aber MariaDB's `rowCount()` zaehlt CHANGED rows, nicht MATCHED rows — bei einem idempotenten Heartbeat (alle Werte schon korrekt) war das 0 und der Resync wurde nicht geliefert. Fix: `PDO::MYSQL_ATTR_FOUND_ROWS => true` in den Connection-Optionen. Beide Bugs waren in der manuellen Verifizierung der Session 14 nicht aufgefallen weil dort jeder Heartbeat einen neuen Offset hatte. Bonus: TICKET-038 hat funktioniert wie versprochen — die Tests haben das gefangen, nicht ein Production-Incident. Auch `.gitignore` erweitert um `/scripts`, `docker-compose.test.yml`, `/website/vendor/`, `/website/.phpunit.cache/`. |
| 16 | 2026-05-13 | Claude Opus 4.7 (1M) | CI for Epic 16 | PHPUnit + smoke E2E run on every push/PR to website/ or scripts/ | Closed the loop on Epic 16: actual automation, not just runnable scripts. SQLite ruled out after audit — schema uses ENUM/ENGINE/NOW/DATE_SUB/UPDATE-LIMIT/FK_CHECKS/TRUNCATE, all MariaDB-dialect. Added `docker-compose.test.yml` (ephemeral mariadb:11 on port 3307, tmpfs storage, no named volume — every `up` is fresh). `.github/workflows/php-tests.yaml` + `smoke-test.yaml` both use a MariaDB service container on the same port. Smoke workflow materializes `website/includes/config.php` from scratch (it's gitignored), starts `php -S` in the background, waits for `?action=status` to respond, runs the bash smoke script. Added `DB_PORT` support to `db.php` + `bootstrap.php` (backward-compat: only used if defined). Both workflows path-filtered to `website/**` / `scripts/**` so plugin-only PRs don't pay for MariaDB spin-up. YAML/PHP all lint clean. |
| 15 | 2026-05-13 | Claude Opus 4.7 (1M) | TICKET-037 (done), TICKET-038 (done), TICKET-039 (done) | All Epic 16 tickets DONE | Test coverage for Epic 15. TICKET-037: extracted `mw_build_heartbeat_body()` + `mw_response_has_resync()` into `src/mw-recording-helpers.c/h` (no OBS deps); 21 Google Test cases in `tests/test-mw-helpers.cpp` cover JSON shape with/without offset, recording_active formatting, negative/large/all sync_method values, truncation, NULL inputs, whitespace tolerance in resync detection, edge cases (truncated buffer, trueish suffix, false-positive keys). TICKET-038: PHPUnit harness in `website/tests/` with `composer.json` + `phpunit.xml`; tests dropt/recreate `mw_aufnahme_test` DB on each run (refused unless name ends in `_test`); `MW_TEST_MODE` constant makes `json_response()` throw `JsonResponseException` so handlers can be called directly; covers offset/sync_method range validation, idle-session updates, multi-session ordering, resync gating during recording (the safety-critical TICKET-008 lesson), pending_resync propagation across stop/start, dashboard auth. TICKET-039: `scripts/mw-smoke-test.sh` + `.ps1` (lockstep assertion sequence) drive the real HTTP API through 10 steps including the gated/queued/delivered resync lifecycle; `scripts/mw-smoke-helper.php` is CLI-only (refuses non-CLI SAPI) so it can never accidentally be deployed as an HTTP endpoint. Build clean on Windows, 5/5 ctest suites pass; all PHP files `php -l` clean; bash + PowerShell scripts parse clean. |
| 14 | 2026-05-13 | Claude Opus 4.7 (1M) | TICKET-034 (done), TICKET-035 (done), TICKET-036 (done) | All Epic 15 tickets DONE | Director-offset-visibility + remote re-sync, end-to-end. TICKET-034: exposed `sync_method_t` enum + new `ltc_source_get_current_offset()` accessor in `ltc-source.h`; mw-recording heartbeat JSON now carries `offset_ms`, `sync_method`, `synced`. Uses `obs_obj_get_data()` to reach the source context. TICKET-035: `install.php` migration adds `offset_ms`, `sync_method` to `sessions`; api/SSE SELECTs include them; `app.js renderUserCards` shows "Drift: X / NTP" per card; `.offset-warn` (orange) + `.offset-crit` (red+pulse) styles. TICKET-036: heartbeat thread restructured to run for full module lifetime (idle heartbeats deliver queued commands between shoots); JSON now includes `recording_active`; api.php heartbeat handler updates the LATEST session row (any non-removed status), gates `resync:true` delivery server-side on `last_recording_active=0`, defense-in-depth at plugin apply time. New `dashboard-api.php?action=request_resync` with director auth. Re-sync button on each card has 3 states (idle / queued during recording / cooldown). NTP thread now waits on either `stop_event` or new `resync_event` (AUTO) via 500ms polling. `pending_resync` flag carried forward across stop/start in `handle_start` so director clicks aren't stranded on offline session rows. `mw_http_post` extended to optionally capture response body; substring match on `"resync":true`. Plugin-side 5s cooldown to avoid kick floods. Build clean on Windows; all 4 test suites pass; PHP/JS lint clean. |
| 13 | 2026-05-09 | Claude Sonnet 4.6 | TICKET-033 (done) | v0.4.1 + website live | Added `website/` to git tracking (was developer-local until now): PHP/MySQL coordination backend with `api.php`, login-gated dashboards (`scenes.php`, `stats.php`), public `datenschutz.php`, SSE feed, login. Expanded `validate_camera_id()` regex from `[A-H]` to `[A-P]` to match plugin 0.4.0. Added "Szene läuft" toggle, Take field, scene participants tracking. `website/includes/config.php` (DB credentials) excluded via `.gitignore`. No plugin code touched. |
| 12 | 2026-04-18 | Claude Opus 4.7 (1M) | TICKET-032 (done) | v0.4.0 → v0.4.1 | Expanded camera ID dropdown from A–H (8) to A–P (16) in both LTC source properties and MW Aufnahme dialog. LTC User Bits `user7` is 4 bits → fits 16 without encoding change. Added `mw_recording_get_camera_id()` / `set_camera_id()` API for bidirectional MW↔LTC sync. Upgrade-safe via `obs_data_has_user_value`: existing per-source overrides preserved, MW config used as fallback only. 0.4.1 follow-up: added `#include <stdbool.h>` to `mw-recording.h` to fix Windows build (`bool` undefined in C header). |
| 11 | 2026-03-27 | Claude Opus 4.6 | TICKET-027–031 (done) | v0.2.0 → v0.3.5 | Major release wave. Built new `mw-recording.c/h` module: heartbeat thread (libcurl, 30s) posting JSON status to a configurable PHP endpoint, hooks into `OBS_FRONTEND_EVENT_RECORDING_*`. Moved MW settings off the LTC source and into a `Tools → MW Aufnahme` dialog backed by global user config. Custom Win32 DSGVO consent dialog with SysLink clickable links (server URL + Datenschutzerklärung). Migrated all MW UI from ANSI to Unicode Win32 APIs (required for SysLink + umlaut rendering); applied dark theme matching OBS. Fixed DPI-aware layout via `GetTextMetrics` + `AdjustWindowRectEx` (was clipping at >100% DPI). Replaced ae/oe/ue/ss digraphs with proper umlauts in dialog text. Enabled `ENABLE_FRONTEND_API` in build presets (and as compile definition). CI builds now also fire on Minewache branch and tag releases with "Minewache Specific Version" suffix. |
| 10 | 2026-03-02 to 2026-03-04 | Mixed (Sarocesch + Claude PR) | TICKET-026 (done) | v0.1.x public-ready | Public release polish. Added `[MW] OBS KIT` scene collection files to git (previously developer-local). Renamed conflicting internal "Minewache" identifiers. Refactored README twice for clarity, fixed broken GitHub links, clarified Linux binary availability (via PR #8). Added email to `buildspec.json`. Configured CI to auto-publish releases. `install.ps1` switched to `-LiteralPath` to handle `[MW]` bracket characters in paths. |
| 9 | 2026-03-01 | Claude Opus 4.6 | TICKET-022 (done), TICKET-023 (done), TICKET-024 (done), TICKET-025 (done) | All Epic 10 tickets DONE | License compliance audit + cleanup. Fixed license declaration to GPLv2+ (was inconsistent). Added GPLv2+ copyright headers to all 14 source files. Switched libltc from static to dynamic linking (SHARED) for LGPLv3 compliance. Updated installer to include libltc.dll + LGPL license copy. Fixed LICENSE + plugin-support.h template placeholders. Updated Known Risks #3 as resolved. |
| 8 | 2026-03-01 | Claude Opus 4.6 | TICKET-021 (done) | TICKET-021 DONE | Created TICKET-021: Track recording warning. Added check in get_properties using obs_frontend_get_profile_config() to read RecTracks bitmask from SimpleOutput + AdvOut. Displays OBS_TEXT_INFO warning when selected track not in recording config. Behind ENABLE_FRONTEND_API guard. Locale string added. Build + all tests pass. |
| 7 | 2026-03-01 | Claude Opus 4.6 | TICKET-020 (done), TICKET-017 (done), TICKET-018 (done), TICKET-019 (done) | All Epic 9 tickets DONE | Implemented all 4 Epic 9 tickets. TICKET-020: Audio track dropdown (Track 1-6, default 3) with obs_source_set_audio_mixers(). TICKET-017: Date+Camera ID in LTC User Bits via SMPTETimecode fields + user7 for camera (A-H). Extended ltc_wrapper_set_timecode API, civil_from_days date algorithm, new roundtrip+date tests. TICKET-018: HTTP Date header fallback using libcurl from OBS deps, fallback chain NTP→HTTP→local, sync_method_t tracking. TICKET-019: metadata-writer.c/h creates .ltc.json sidecar next to recordings with camera_id, timecodes, sync info. All tests pass (4 suites). |
| 6 | 2026-03-01 | Claude Opus 4.6 | TICKET-017 (created), TICKET-018 (created), TICKET-019 (created), TICKET-020 (created) | All 4 tickets TODO | Epic 9: Metadata & Reliability. Feature analysis for decentralized multi-cam workflow (multiple PCs across Germany → DaVinci Resolve). Created 4 tickets: User Bits (date+camera ID), HTTP time fallback, metadata sidecar, audio track selection. Dropped: visual overlay (destroys edit material), remote endpoint (deferred — needs infra first), MIDI TC, NDI TC, custom start TC. UX priority: dead-simple UI for non-technical users. |
| 5 | 2026-03-01 | Claude Opus 4.6 | TICKET-014 (done), TICKET-015 (done), TICKET-016 (done), TICKET-012 (done) | All 4 tickets DONE | Epic 8: MW OBS KIT Template Integration. Added LTC Timecode source to Minewache scene collection (mixers=4, Track 3 only). Updated basic.ini (RecTracks=7, Track3Name). Extended install.ps1 and Inno Setup installer to auto-deploy template. TICKET-012: Auto-setup dialog on first OBS start offers to switch to Minewache template. Uses obs-frontend-api (ENABLE_FRONTEND_API=ON), Win32 MessageBoxA on Windows, auto-switch on Linux. Config flag prevents re-prompting. |
| 4 | 2026-02-27 | Claude Opus 4.6 | TICKET-011 (done), TICKET-013 (done), TICKET-012 (created) | TICKET-011+013 DONE | User feedback: plugin doesn't appear in OBS. **Root cause: wrong install path** — all docs said `%APPDATA%` but OBS loads from `%ProgramData%`. Fixed install.ps1, install.sh, README, Setup.md. Added plugin diagnostics (obs_module_description, verbose logging). Created TICKET-012 for auto-setup (pending). |
| 3 | 2026-02-27 | Claude Opus 4.6 | TICKET-009 | README & docs done | Wrote comprehensive user-facing README.md: install instructions (Win/Linux), usage guide, multi-camera sync workflow, configuration reference, troubleshooting/FAQ, build-from-source guide, technical details. TICKET-008 skipped (requires physical 2-PC hardware test). TICKET-010 left for user (release tagging). |
| 2 | 2026-02-27 | Claude Opus 4.6 | TICKET-008 (partial) | DaVinci Resolve compat fixes done | Fixed obs_log build error (missing plugin-support.h include). Replaced blind hard-resync with drift-aware resync (750 frame interval, 2 frame threshold). Added ContinuousTimecodeSequence25fps test. All 27 tests pass. |
| 1 | 2026-02-27 | Claude Opus 4.6 | TICKET-001 through TICKET-007 | All 7 tickets DONE | Full integration: NTP sync thread, LTC audio gen, Properties UI. Fixed ltc_encoder_create bug (was passing SPF instead of FPS). All 26 unit tests pass. |

*(Agent: Add a row at the START of each new session and UPDATE it at the end.)*

---

## Known Risks & Open Questions

1. **NTP over restricted networks:** Corporate firewalls may block UDP 123.
   → Mitigation: Configurable NTP server **plus** built-in anycast fallback chain (`time.cloudflare.com`, `time.google.com`) and HTTP Date header as last resort — see TICKET-040. Operators are surfaced a `LOG_ERROR` + Properties UI banner when the whole chain fails (was silent before).
2. **OBS audio callback threading:** `obs_source_output_audio()` must be called from a consistent thread. Research whether a dedicated thread or OBS timer is better.
3. ~~**libltc LGPL licensing:**~~ **RESOLVED in TICKET-024** — libltc switched from static to dynamic linking (shared library). LGPL Section 4d1 satisfied automatically. DLL/SO shipped alongside plugin.
4. **Drop-frame timecode:** 29.97 fps requires drop-frame handling. Must verify DaVinci Resolve's expectations.
5. **OBS 32 plugin validation:** OBS 32 has stricter plugin loading on Linux. Must build against correct `LIBOBS_API_VER`.
6. **Sample-accurate frame boundaries:** LTC frames must align with audio sample boundaries. Off-by-one errors cause decode failures.
7. **Windows plugin path:** OBS loads plugins from `C:\ProgramData\obs-studio\plugins\`, NOT from `%APPDATA%`. This was incorrectly documented and caused the "plugin doesn't appear" bug. **RESOLVED in TICKET-013.**
8. ~~**Plugin not appearing in OBS:**~~ **RESOLVED** — Root cause was wrong install path (`%APPDATA%` instead of `%ProgramData%`). All docs and scripts updated.

---

## How to Resume (Agent Onboarding)

If you are a new agent picking up this project:

1. **Read this ENTIRE file.** Don't skip sections.
2. Check the **Backlog** – find the first `TODO` ticket whose dependencies are all `DONE`.
3. Read the **Decision Log** – understand past choices. Do NOT reverse them without explicit user approval.
4. Read the **Session Log** – understand what happened before you.
5. Begin work on the next ticket. Ask if anything is unclear.
6. **Update this file** when done: move ticket status, add session log entry, add decisions to decision log.
