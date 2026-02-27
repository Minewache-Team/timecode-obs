# Security & Bug Audit Report — obs-ltc-timecode

**Date:** 2026-02-27
**Scope:** Full codebase review before production release
**Files reviewed:** All 9 source files (src/), 3 test files, 2 install scripts, build config

---

## CRITICAL Issues (Fixed)

### 1. Framerate auto-detect misidentifies 30fps as 29.97df
**File:** `src/ltc-source.c` — `detect_obs_framerate()`
**Impact:** Any user with OBS set to 30fps would get drop-frame timecode (29.97df), producing wrong timecodes in recordings and breaking DaVinci Resolve sync.
**Root cause:** The check `fabs(fps - 29.97) < 0.5` matches 30.0 (since |30.0 - 29.97| = 0.03 < 0.5), and it runs before the 30fps check.
**Fix:** Check exact OBS integer ratios (fps_num/fps_den) first, then fall back to approximate matching with tighter thresholds and correct ordering (exact rates before NTSC counterparts).

### 2. Windows NTP socket timeout uses wrong type
**File:** `src/ntp-client.c` — `ntp_query()`
**Impact:** On Windows, `SO_RCVTIMEO` expects a `DWORD` (milliseconds) not `struct timeval`. The code passed a `struct timeval`, causing Windows to interpret the first 4 bytes (tv_sec=2 for 2000ms timeout) as 2ms. NTP queries would nearly always timeout on Windows.
**Fix:** Platform-conditional timeout: `DWORD` on Windows, `struct timeval` on Linux/macOS.

---

## MEDIUM Issues (Fixed)

### 3. NTP server hostname data race
**File:** `src/ltc-source.c` — `ltc_source_update()` / `ntp_sync_thread()`
**Impact:** `ltc_source_update()` could write to `ctx->ntp_server` while the NTP thread reads it, causing a data race. Could lead to failed DNS resolution or (theoretically) garbled memory access.
**Fix:** Protected `ntp_server` access with `encoder_mutex`. NTP thread copies the server name under lock. Server changes now restart the NTP thread for immediate effect.

### 4. No NTP response validation
**File:** `src/ntp-client.c` — `ntp_query()`
**Impact:** The NTP client accepted any 48-byte UDP response without validating it. A network attacker could spoof NTP responses to manipulate timecodes. Invalid servers (stratum 0 KoD packets, unsynchronized servers) were also accepted.
**Fix:** Added validation: reject responses with invalid mode (!= server/broadcast), invalid stratum (0 or >15), or zero transmit timestamp.

---

## LOW Issues (Fixed)

### 5. macOS bundleId placeholder in buildspec.json
**File:** `buildspec.json`
**Impact:** macOS builds would use `com.example.plugintemplate-for-obs` as the bundle identifier, which could conflict with other OBS plugins using the same template default.
**Fix:** Changed to `com.ferdmusic.obs-ltc-timecode`.

### 6. No validation on framerate enum cast from settings
**File:** `src/ltc-source.c` — `ltc_source_create()` / `ltc_source_update()`
**Impact:** An invalid `framerate` value in OBS settings (e.g. from corrupted config) would be cast directly to `tc_framerate_t`, potentially creating an encoder with an unsupported framerate (resulting in NULL encoder and silent audio).
**Fix:** Added `fps_setting_valid()` helper. Invalid values fall back to TC_FPS_25 on create, or keep the current framerate on update.

---

## Informational (Not Fixed — Documented)

### 7. `volatile` for cross-thread int64_t communication
**File:** `src/ltc-source.c` — `ntp_offset_ms`, `ntp_synced`, `ntp_roundtrip_ms`
**Concern:** `volatile` does not guarantee atomic reads/writes in the C standard. However, on x86-64 (the only target platforms), aligned 64-bit reads/writes are atomic at the hardware level. Single-writer/single-reader pattern mitigates risk.
**Recommendation:** Consider C11 `_Atomic` in a future refactor if MSVC compatibility permits.

### 8. Properties UI shows labels but no dynamic values
**File:** `src/ltc-source.c` — `ltc_source_get_properties()`
**Concern:** The "NTP Status" and "Current Timecode" info fields are computed but the values are never set on the OBS properties. Users see labels without values.
**Recommendation:** Use `obs_data_set_string()` on the settings to populate these fields, or implement a `modified_callback` to update them dynamically.

### 9. `getaddrinfo()` blocks without timeout
**File:** `src/ntp-client.c` — `ntp_query()`
**Concern:** DNS resolution via `getaddrinfo()` has no timeout mechanism. On networks with unresponsive DNS, this could block the NTP thread indefinitely.
**Recommendation:** Accept this as a known limitation. The NTP thread is non-critical — the plugin falls back to local clock if NTP is unavailable.

### 10. Placeholder email in buildspec.json
**File:** `buildspec.json` — `"email": "me@example.com"`
**Concern:** Should be updated to the real maintainer email before release.

### 11. CMakePresets.json literal CMAKE_SYSTEM_PROCESSOR
**File:** `CMakePresets.json` — `"CMAKE_INSTALL_LIBDIR": "lib/CMAKE_SYSTEM_PROCESSOR-linux-gnu"`
**Concern:** This string is not a CMake variable expansion — it's literal text. The install path on Linux would be `lib/CMAKE_SYSTEM_PROCESSOR-linux-gnu` instead of `lib/x86_64-linux-gnu`. However, this only affects `make install` (not used in the current workflow — the install scripts handle deployment).

---

## Dependency & Infrastructure Audit

| Check | Status |
|---|---|
| Hardcoded secrets | PASS — None found |
| .env files committed | PASS — None exist |
| .gitignore coverage | PASS — Strong allowlist pattern |
| SQL/Command/XSS injection | PASS — N/A (native plugin) |
| Unsafe C string functions | PASS — Uses `snprintf()` throughout |
| Buffer overflows | PASS — All buffers are size-bounded |
| GitHub Actions secrets | PASS — All use `${{ secrets.* }}` |
| Dependency integrity | PASS — SHA256 hashes in buildspec.json |
| HTTPS for downloads | PASS — All URLs use HTTPS |
