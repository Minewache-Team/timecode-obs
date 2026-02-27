# obs-ltc-timecode

An OBS Studio plugin that generates NTP-synchronized Linear Timecode (LTC) audio, enabling frame-accurate multi-camera synchronization in post-production tools like DaVinci Resolve.

## What It Does

This plugin adds an **"LTC Timecode Generator"** audio source to OBS Studio. It:

1. Syncs your system clock to NTP (network time) for millisecond-accurate timestamps
2. Converts the current time to SMPTE timecode (HH:MM:SS:FF)
3. Encodes the timecode as LTC audio (an industry-standard audio signal)
4. Outputs the LTC signal on a dedicated audio track in your recording

When multiple PCs run this plugin, their recordings share the same NTP-derived timecode. Import the recordings into DaVinci Resolve (or any NLE that reads LTC), and they sync automatically — no clapper board needed.

## Features

- **NTP time synchronization** — automatic sync to `pool.ntp.org` with configurable server and interval
- **SMPTE timecode** — supports 24, 25, 29.97 (drop-frame), 30, 50, and 60 fps
- **Auto framerate detection** — reads your OBS output settings, or set manually
- **Continuous LTC audio** — gap-free, sample-accurate encoding at 48 kHz mono
- **Drift-aware resync** — corrects clock drift without causing timecode jumps
- **Lightweight** — no external dependencies besides OBS itself

## Supported Platforms

| Platform | Architecture | Minimum Version |
|----------|-------------|-----------------|
| Windows  | x64         | Windows 10      |
| Linux    | x86_64      | Ubuntu 24.04    |

## Installation

### Windows

**Option A: Install script (recommended)**

After building (or extracting a release), run the install script from the project root:

```powershell
.\install.ps1
```

The script automatically:
- Creates the `plugins` folder if it doesn't exist (this is normal — OBS doesn't create it by default)
- Copies the DLL and locale data to the correct paths
- Verifies the installation

To uninstall: `.\install.ps1 -Uninstall`

**Option B: Manual install**

1. Download the latest release `.zip` from the [Releases](../../releases) page
2. Extract the archive
3. Copy the contents to your OBS plugins folder:
   ```
   %APPDATA%\obs-studio\plugins\obs-ltc-timecode\
   ```

   > **Note:** The `plugins` folder does NOT exist by default in `%APPDATA%\obs-studio\` —
   > you'll only see `plugin_config`. You must create the `plugins` folder yourself. This is normal OBS behavior.

   The folder structure should look like:
   ```
   %APPDATA%\obs-studio\
   ├── plugin_config\          ← already exists (OBS creates this)
   └── plugins\                ← you create this
       └── obs-ltc-timecode\
           ├── bin\
           │   └── 64bit\
           │       └── obs-ltc-timecode.dll
           └── data\
               └── locale\
                   └── en-US.ini
   ```
4. Restart OBS Studio

### Linux

**Option A: Install script (recommended)**

After building, run the install script from the project root:

```bash
chmod +x install.sh
./install.sh
```

To uninstall: `./install.sh --uninstall`

**Option B: .deb package (Ubuntu/Debian)**
```bash
sudo dpkg -i obs-ltc-timecode_*.deb
```

**Option C: Manual install**
```bash
tar xzf obs-ltc-timecode_*.tar.gz
mkdir -p ~/.config/obs-studio/plugins/obs-ltc-timecode/bin/64bit
mkdir -p ~/.config/obs-studio/plugins/obs-ltc-timecode/data
cp obs-ltc-timecode.so ~/.config/obs-studio/plugins/obs-ltc-timecode/bin/64bit/
cp -r data/* ~/.config/obs-studio/plugins/obs-ltc-timecode/data/
```

Restart OBS Studio after installation.

## Usage

### Basic Setup

1. In OBS, click **Sources** → **+** → **LTC Timecode Generator**
2. The plugin appears in the Audio Mixer with LTC output
3. In **Settings** → **Output** → **Recording**, assign the LTC source to a **separate audio track** (e.g., Track 4) so it doesn't mix with your main audio

### Recording for Multi-Camera Sync

1. Install the plugin on each recording PC
2. Ensure all PCs have internet access (for NTP sync)
3. Add the LTC source on each PC, assigned to the same track number
4. Use the same framerate setting on all PCs (or leave on "Auto" if OBS settings match)
5. Start recordings — they don't need to start at the exact same time
6. Import recordings into DaVinci Resolve and use the LTC audio track for timecode-based synchronization

### Configuration

Open the source properties to configure:

| Setting | Description | Default |
|---------|-------------|---------|
| **Framerate** | Timecode framerate. "Auto" reads from OBS output settings. | Auto (from OBS) |
| **NTP Server** | NTP server address for time synchronization. | `pool.ntp.org` |
| **Sync Interval** | How often the plugin re-syncs with the NTP server. | 5 min |

The properties panel also shows:
- **NTP Status** — whether synchronization was successful and the current offset
- **Current Timecode** — the live SMPTE timecode being encoded

## Troubleshooting

### Plugin doesn't appear in OBS

**Step-by-step checklist:**

1. **Check the folder structure** — the most common issue:
   - Windows: Open `%APPDATA%\obs-studio\plugins\obs-ltc-timecode\` in Explorer
   - The `plugins` folder does NOT exist by default — you must create it (or use `install.ps1`)
   - Verify `bin\64bit\obs-ltc-timecode.dll` exists
   - Verify `data\locale\en-US.ini` exists

2. **Check the OBS log** — `Help` → `Log Files` → `View Current Log`:
   - Search for `obs-ltc-timecode` in the log
   - If you see `"loading plugin (version ...)"` → plugin loads, issue is elsewhere
   - If you see no mention of the plugin → OBS didn't find the DLL
   - If you see `"error loading module"` → DLL dependency issue (see below)

3. **Verify 64-bit OBS** — this plugin only works with 64-bit OBS Studio

4. **Reinstall with the install script** — run `.\install.ps1` which verifies everything:
   ```powershell
   .\install.ps1
   ```

### NTP sync fails / "Not synced — using local clock"

- **Firewall**: NTP uses **UDP port 123**. Corporate networks and some firewalls block this. Ask your network admin or use a VPN.
- **Custom NTP server**: If `pool.ntp.org` is blocked, try your organization's internal NTP server in the plugin settings.
- **Local clock fallback**: The plugin will still work using your local system clock, but multi-PC sync may drift without NTP.

### Timecode drift in long recordings

- Ensure NTP sync is working (check the status in plugin properties)
- Use a shorter sync interval (1 min) for better accuracy
- The plugin's drift-aware resync corrects up to 2 frames of drift automatically

### DaVinci Resolve doesn't read the timecode

- Record in MKV or MOV format (both support multiple audio tracks)
- Make sure the LTC audio is on a separate track, not mixed with other audio
- In DaVinci Resolve's Media Pool, right-click the clip → check timecode track settings
- Verify the audio track is not silent (check the Audio Mixer level in OBS during recording)

## Building from Source

### Prerequisites

| Tool | Windows | Linux (Ubuntu 24.04+) |
|------|---------|----------------------|
| CMake 3.28+ | [cmake.org](https://cmake.org/download/) | `sudo apt install cmake` |
| Visual Studio 2022 | [visualstudio.microsoft.com](https://visualstudio.microsoft.com/) | — |
| GCC / build tools | — | `sudo apt install build-essential` |
| Ninja | Via VS Installer | `sudo apt install ninja-build` |

### Build Steps

```bash
# Clone with submodules
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

### Running Tests

The project includes unit tests for timecode generation, NTP offset calculation, and LTC encode/decode roundtrips:

```bash
cd build_x86_64  # or build_x64 on Windows
ctest --output-on-failure
```

## Technical Details

- **Audio format**: 48 kHz, mono, float32, -12 dBFS amplitude
- **LTC encoding**: Uses [libltc](https://github.com/x42/libltc) (LGPL-3.0)
- **NTP protocol**: SNTPv4 (RFC 4330), no admin privileges required
- **Thread model**: Dedicated NTP sync thread + OBS video tick for audio generation
- **Drift correction**: Checks every ~30 seconds, hard-resyncs only if drift exceeds 2 frames

## License

This plugin is licensed under the [GNU General Public License v2.0](LICENSE).

It uses [libltc](https://github.com/x42/libltc) which is licensed under LGPL-3.0. Object files are available for relinking upon request.

## Credits

- **Author**: [Ferdmusic](https://github.com/Ferdmusic)
- **LTC encoding**: [libltc](https://github.com/x42/libltc) by Robin Gareus
- **Plugin template**: [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) by OBS Project
