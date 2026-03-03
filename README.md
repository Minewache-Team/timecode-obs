# obs-ltc-timecode

**An OBS Studio plugin that generates NTP-synchronized Linear Timecode (LTC) audio for frame-accurate multi-camera synchronization.**

> **Note:** This project is no longer actively maintained. Forks are welcome — feel free to pick it up and build on it!

---

## Overview

When recording with multiple PCs, syncing footage in post-production is a pain. This plugin solves that by embedding an NTP-synchronized SMPTE timecode as LTC audio into your OBS recordings. Import the clips into DaVinci Resolve (or any NLE that reads LTC) and they sync automatically — no clapper board needed.

### How It Works

1. Syncs your system clock to an NTP server for millisecond-accurate timestamps
2. Converts the current time to SMPTE timecode (HH:MM:SS:FF)
3. Encodes the timecode as an LTC audio signal
4. Outputs it on a dedicated audio track in your recording

Run this on every recording PC → all recordings share the same timecode → instant sync in post.

---

## Features

- **NTP time sync** — automatic sync to `pool.ntp.org` (configurable server & interval)
- **SMPTE timecode** — 24, 25, 29.97 (drop-frame), 30, 50, 60 fps
- **Auto framerate detection** — reads your OBS output settings, or set manually
- **Continuous LTC audio** — gap-free, sample-accurate encoding at 48 kHz mono
- **Drift-aware resync** — corrects clock drift without timecode jumps
- **Lightweight** — no external dependencies besides OBS itself

---

## Platform Availability

| Platform | Status | Releases |
|----------|--------|----------|
| **Windows** (x64) | Fully supported | Available on [Releases](../../releases) page |
| **Linux** (x86_64) | Builds in CI | Not in releases* |

> \* **Linux:** The plugin builds successfully for Linux (Ubuntu 24.04+) in our GitHub Actions CI pipeline, but Linux binaries are currently **not included in the releases**. Since no one on the team is currently on Linux, we chose not to upload them to avoid confusion with untested artifacts.
>
> **If you need the Linux build**, you have two options:
> 1. **Download from CI** — go to the [Actions](../../actions) tab, select a successful workflow run, and download the Linux artifact
> 2. **Open an issue** — as soon as the first person requests it, we'll start including Linux binaries in the releases

---

## Installation

### Windows

**Option A: Installer (recommended)**

1. Download the latest installer (`.exe`) from the [Releases](../../releases) page
2. Run the installer — it handles everything automatically
3. Restart OBS Studio

**Option B: Manual install**

1. Download the latest `.zip` from the [Releases](../../releases) page
2. Extract and copy the contents to:
   ```
   C:\ProgramData\obs-studio\plugins\obs-ltc-timecode\
   ├── bin\
   │   └── 64bit\
   │       └── obs-ltc-timecode.dll
   └── data\
       └── locale\
           └── en-US.ini
   ```
3. Restart OBS Studio

> **Important:** The correct path is `C:\ProgramData\obs-studio\plugins\` — **not** `%APPDATA%\obs-studio\plugins\`. OBS does not load plugins from AppData.

### Linux

**Option A: Install script**
```bash
chmod +x install.sh && ./install.sh
```

**Option B: .deb package**
```bash
sudo dpkg -i obs-ltc-timecode_*.deb
```

**Option C: Manual install**
```bash
mkdir -p ~/.config/obs-studio/plugins/obs-ltc-timecode/bin/64bit
mkdir -p ~/.config/obs-studio/plugins/obs-ltc-timecode/data
cp obs-ltc-timecode.so ~/.config/obs-studio/plugins/obs-ltc-timecode/bin/64bit/
cp -r data/* ~/.config/obs-studio/plugins/obs-ltc-timecode/data/
```

To uninstall: `./install.sh --uninstall`

---

## Usage

### Basic Setup

1. In OBS, go to **Sources** → **+** → **LTC Timecode Generator**
2. The plugin appears in the Audio Mixer with LTC output
3. In **Settings** → **Output** → **Recording**, assign the LTC source to a **separate audio track** (e.g., Track 4)

### Multi-Camera Sync

1. Install the plugin on each recording PC
2. Ensure all PCs have internet access (for NTP)
3. Add the LTC source on each, assigned to the same track number
4. Use the same framerate on all PCs (or leave on "Auto")
5. Start recordings — they don't need to start simultaneously
6. Import into DaVinci Resolve and sync via the LTC audio track

### Configuration

| Setting | Description | Default |
|---------|-------------|---------|
| **Framerate** | Timecode framerate. "Auto" reads from OBS. | Auto |
| **NTP Server** | Server address for time sync. | `pool.ntp.org` |
| **Sync Interval** | Re-sync frequency. | 5 min |

The properties panel also shows the **NTP sync status** and the **live SMPTE timecode** being generated.

---

## Troubleshooting

<details>
<summary><strong>Plugin doesn't appear in OBS</strong></summary>

1. **Check the install path** — most common mistake:
   - **Correct:** `C:\ProgramData\obs-studio\plugins\obs-ltc-timecode\`
   - **Wrong:** `%APPDATA%\obs-studio\plugins\` (OBS doesn't load plugins from here)

2. **Verify the folder structure** — `bin\64bit\obs-ltc-timecode.dll` and `data\locale\en-US.ini` must exist

3. **Check the OBS log** (`Help` → `Log Files` → `View Current Log`):
   - `"loading plugin"` → plugin found, issue is elsewhere
   - No mention → wrong install path
   - `"error loading module"` → DLL dependency issue

4. **Verify 64-bit OBS** — 32-bit is not supported

5. **Re-run the installer** from the [Releases](../../releases) page

</details>

<details>
<summary><strong>NTP sync fails / "Not synced — using local clock"</strong></summary>

- **Firewall:** NTP uses UDP port 123. Corporate networks may block this.
- **Custom server:** Try your organization's internal NTP server.
- **Fallback:** The plugin still works with your local clock, but multi-PC sync may drift.

</details>

<details>
<summary><strong>Timecode drift in long recordings</strong></summary>

- Verify NTP sync is active (check status in plugin properties)
- Use a shorter sync interval (1 min)
- The drift-aware resync corrects up to 2 frames automatically

</details>

<details>
<summary><strong>DaVinci Resolve doesn't read the timecode</strong></summary>

- Record in MKV or MOV (both support multiple audio tracks)
- Ensure LTC audio is on a separate track, not mixed with other audio
- In Resolve's Media Pool, right-click the clip and check timecode track settings
- Verify the track isn't silent (check Audio Mixer levels in OBS)

</details>

---

## Building from Source

### Prerequisites

| Tool | Windows | Linux (Ubuntu 24.04+) |
|------|---------|----------------------|
| CMake 3.28+ | [cmake.org](https://cmake.org/download/) | `sudo apt install cmake` |
| Visual Studio 2022 | [visualstudio.microsoft.com](https://visualstudio.microsoft.com/) | — |
| GCC / build tools | — | `sudo apt install build-essential` |
| Ninja | Via VS Installer | `sudo apt install ninja-build` |

### Build

```bash
git clone --recursive https://github.com/Minewache-Team/timecode-obs.git
cd timecode-obs
```

**Windows (PowerShell):**
```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
cd build_x64 && ctest --output-on-failure
```

**Linux:**
```bash
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64
cd build_x86_64 && ctest --output-on-failure
```

---

## Technical Details

| Property | Value |
|----------|-------|
| Audio format | 48 kHz, mono, float32, -12 dBFS |
| LTC encoding | [libltc](https://github.com/x42/libltc) (LGPL-3.0) |
| NTP protocol | SNTPv4 (RFC 4330), no admin privileges required |
| Thread model | Dedicated NTP sync thread + OBS video tick |
| Drift correction | Checks every ~30s, hard-resyncs if drift > 2 frames |

---

## License

Licensed under the [GNU General Public License v2.0 or later](LICENSE) (GPL-2.0-or-later).

Uses [libltc](https://github.com/x42/libltc) (LGPL-3.0), dynamically linked to comply with LGPL requirements.

## About

This plugin was built by a small team with domain knowledge in audio, video, and timecode workflows. AI (Claude Code) was used as a development multiplier. See [SYSTEM_PROMPT.md](SYSTEM_PROMPT.md) and [PROJECT.md](PROJECT.md) for details on the AI-assisted workflow.

## Credits

- **Author:** [Ferdmusic](https://github.com/Ferdmusic)
- **LTC encoding:** [libltc](https://github.com/x42/libltc) by Robin Gareus
- **Plugin template:** [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) by OBS Project
