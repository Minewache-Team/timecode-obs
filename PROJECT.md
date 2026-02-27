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
| Target Platforms | Windows 10+ (x64), Linux (Ubuntu 24.04+)  |
| OBS SDK Version  | 32.x (current stable)                      |
| License          | GPLv2 (OBS compatibility)                  |
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
- **Status:** `TODO`
- **Depends on:** TICKET-008
- **Type:** Documentation
- **Description:** Write user-facing README with install instructions, usage guide, FAQ.
- **Acceptance Criteria:**
  - [ ] Clear install instructions for Windows and Linux
  - [ ] Usage guide with screenshots
  - [ ] Troubleshooting section
  - [ ] Link to releases page

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

---

## Session Log

| Session | Date | Agent | Tickets Worked | Status at End | Notes |
|---------|------|-------|----------------|---------------|-------|
| 2 | 2026-02-27 | Claude Opus 4.6 | TICKET-008 (partial) | DaVinci Resolve compat fixes done | Fixed obs_log build error (missing plugin-support.h include). Replaced blind hard-resync with drift-aware resync (750 frame interval, 2 frame threshold). Added ContinuousTimecodeSequence25fps test. All 27 tests pass. |
| 1 | 2026-02-27 | Claude Opus 4.6 | TICKET-001 through TICKET-007 | All 7 tickets DONE | Full integration: NTP sync thread, LTC audio gen, Properties UI. Fixed ltc_encoder_create bug (was passing SPF instead of FPS). All 26 unit tests pass. |

*(Agent: Add a row at the START of each new session and UPDATE it at the end.)*

---

## Known Risks & Open Questions

1. **NTP over restricted networks:** Corporate firewalls may block UDP 123.
   → Mitigation: Configurable NTP server. Future: HTTP-based time API fallback.
2. **OBS audio callback threading:** `obs_source_output_audio()` must be called from a consistent thread. Research whether a dedicated thread or OBS timer is better.
3. **libltc LGPL licensing:** libltc is LGPLv3. Statically linking into a GPL-2 plugin is allowed but requires making object files available for relinking. Alternative: dynamic linking.
4. **Drop-frame timecode:** 29.97 fps requires drop-frame handling. Must verify DaVinci Resolve's expectations.
5. **OBS 32 plugin validation:** OBS 32 has stricter plugin loading on Linux. Must build against correct `LIBOBS_API_VER`.
6. **Sample-accurate frame boundaries:** LTC frames must align with audio sample boundaries. Off-by-one errors cause decode failures.

---

## How to Resume (Agent Onboarding)

If you are a new agent picking up this project:

1. **Read this ENTIRE file.** Don't skip sections.
2. Check the **Backlog** – find the first `TODO` ticket whose dependencies are all `DONE`.
3. Read the **Decision Log** – understand past choices. Do NOT reverse them without explicit user approval.
4. Read the **Session Log** – understand what happened before you.
5. Begin work on the next ticket. Ask if anything is unclear.
6. **Update this file** when done: move ticket status, add session log entry, add decisions to decision log.
