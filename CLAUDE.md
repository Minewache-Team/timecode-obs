# CLAUDE.md

This file gives AI agents (Claude Code and friends) the minimum context needed to
work productively in this repository. Read it on every session start.

---

## Single Source of Truth

**`PROJECT.md` is the single source of truth.** Read it fully before doing any
work. It carries the ticket backlog, decision log, session log, and known risks.

`SYSTEM_PROMPT.md` describes the role, working method, and code standards expected
of an agent working on this project. Follow those rules — they are not optional.

## Workflow Rules (excerpted from SYSTEM_PROMPT.md)

- **Ticket-based.** Never implement anything without a ticket in `PROJECT.md`.
- **One ticket per response.** Don't bundle.
- **Update `PROJECT.md`** after completing work: move ticket status, add a session
  log row, add architecturally non-trivial choices to the Decision Log.
- **Complete files only.** No `// ... rest of code` placeholders in code output.
- **State the file path** above any code block you produce, plus whether the
  file is new or modified.

## Project at a Glance

- **What:** OBS Studio plugin that generates SMPTE LTC timecode audio,
  NTP-synced for cross-PC recording sync (DaVinci Resolve workflow).
- **Language:** C17 (plugin), C++17 (tests via Google Test).
- **Branches:**
  - `master` — public release (currently v0.1.x line).
  - `Minewache` — active development line for the Minewache team (currently
    v0.4.1). New features land here first; selectively merged to master.
- **Architecture:** Testable core (NTP, timecode, LTC encoder) + thin OBS
  adapter. The core has zero OBS dependencies. Diagram in `PROJECT.md`.

## Build & Test

```powershell
# Windows (PowerShell, from repo root)
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
ctest --preset windows-x64
```

```bash
# Linux
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64
ctest --preset ubuntu-x86_64
```

- Test sources: `tests/*.cpp` (Google Test, FetchContent).
- All core modules have unit tests. Add or update tests as part of every
  implementation ticket.
- `ENABLE_FRONTEND_API` is ON by default in the build presets; some features
  (`mw-recording.c`, `auto-setup.c`, metadata sidecar) are guarded by it.

## Install (for local manual testing)

- **Windows:** Run `install.ps1` from an elevated PowerShell. It installs to
  `%ProgramData%\obs-studio\plugins\` — **not `%APPDATA%`** (see TICKET-013;
  the wrong-path bug burned us once).
- **Linux:** Run `install.sh`.
- The script also deploys the `[MW] OBS KIT` scene collection (only if it
  doesn't already exist — `-not Test-Path` / `onlyifdoesntexist` semantics).

## Key Source Files

| File | Role |
|------|------|
| `src/plugin-main.c` | OBS entry point: load/unload, source registration |
| `src/ltc-source.c/h` | OBS source: properties, audio callback, NTP thread lifecycle |
| `src/ntp-client.c/h` | SNTP query (Winsock2 / POSIX), background re-sync |
| `src/http-time-client.c/h` | HTTPS Date header fallback when NTP is blocked |
| `src/timecode.c/h` | NTP-corrected wall time → SMPTE TC (HH:MM:SS:FF + date) |
| `src/ltc-encoder-wrapper.c/h` | libltc wrapper: encode SMPTE TC + User Bits → PCM |
| `src/auto-setup.c/h` | First-run: offer to switch to Minewache scene collection |
| `src/metadata-writer.c/h` | `.ltc.json` sidecar next to each recording |
| `src/mw-recording.c/h` | MW Aufnahme: Tools-menu dialog, heartbeat to PHP backend |
| `src/plugin-support.h` | `obs_log()` macro — include it in any file that logs |

## House Rules / Gotchas

- **No new external deps** beyond: OBS SDK, libltc (submodule), Google Test
  (FetchContent), libcurl (already shipped by OBS). Don't pull in JSON
  libraries — write small JSON by hand (see `metadata-writer.c`).
- **libltc is dynamically linked** (LGPLv3 compliance, TICKET-024). Don't
  switch it back to STATIC.
- **No Qt.** `ENABLE_QT=OFF`. UI dialogs use Win32 directly. SysLink controls
  require Unicode (`W`) APIs, not ANSI (TICKET-030).
- **MW settings are global**, not per-source (TICKET-028). Read/write via
  `obs_frontend_get_user_config()`, not `obs_data`.
- **Camera ID range is A–P (16 values)** as of v0.4.0. Fits the 4-bit `user7`
  LTC User Bits field exactly — don't widen it without an encoding change.
- **License is GPLv2+** (`GPL-2.0-or-later`), not GPLv2-only. Every new
  `.c`/`.h` file needs the standard copyright header (see existing files for
  the format).
- **Platform guards:** All Windows-only code behind `#ifdef _WIN32`; POSIX
  alternative behind `#else`. No platform-specific includes leak across.
- **Drop-frame timecode (29.97):** Pass the `tc_framerate_t` enum, never raw
  fps numbers (TICKET-005 was a bug from doing that).

## Where Things Live Outside `src/`

- `tests/` — Google Test suites. Each core module has a `test-<module>.cpp`.
- `cmake/` — `BuildLibLTC.cmake`, helper modules. `BuildLibLTC.cmake` sets the
  library to SHARED for LGPL compliance.
- `installer/obs-ltc-timecode.iss` — Inno Setup installer for Windows.
- `data/locale/en-US.ini` — All user-visible strings. Add new strings here,
  reference via `obs_module_text("KEY")`.
- `[MW] OBS KIT/` — Scene collection + profile shipped to MW team users.
- `website/` — PHP/MySQL coordination backend (Epic 14). Lives at the
  configured MW server URL. `config.php` is gitignored.

## When in Doubt

1. Re-read `PROJECT.md` — the ticket and Decision Log often answer it.
2. Check `git log -- <file>` for the history of the surrounding code.
3. Ask. Don't guess at architectural questions with multiple valid paths.
