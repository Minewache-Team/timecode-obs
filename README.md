# obs-ltc-timecode

An OBS Studio plugin that generates NTP-synchronized Linear Timecode (LTC) audio for multi-camera synchronization.

> **Note:** This project is no longer actively maintained. Forks are welcome.

## Overview

Syncing footage from multiple recording PCs can be tedious. This plugin embeds an NTP-synchronized SMPTE timecode as LTC audio directly into your OBS recordings. You can then import the clips into an NLE like DaVinci Resolve to sync them automatically.

### How it works

1. Syncs the system clock to an NTP server.
2. Converts the current time to SMPTE timecode (HH:MM:SS:FF).
3. Encodes the timecode as an LTC audio signal.
4. Outputs the audio on a dedicated track in your recording.

If you run this on all your recording PCs, every file will share the exact same timecode.

## Features

* NTP time sync to `pool.ntp.org` (server and interval are configurable)
* SMPTE timecode support for 24, 25, 29.97 (drop-frame), 30, 50, and 60 fps
* Framerate detection from OBS output settings (or manual override)
* Gap-free LTC audio encoding at 48 kHz mono
* Clock drift correction
* Minimal dependencies outside of OBS

## Platforms

| Platform | Status | Releases |
| --- | --- | --- |
| **Windows** (x64) | Supported | Available on the [Releases](https://github.com/Minewache-Team/timecode-obs/releases) page |
| **Linux** (x86_64) | Builds in CI | Available on request |

Linux binaries (Ubuntu 24.04+) build successfully in GitHub Actions. Once someone opens an issue requesting a Linux binary, we'll add it to the [Releases](https://github.com/Minewache-Team/timecode-obs/releases). Until then, you can download the artifact from the latest successful build on the [Actions](https://github.com/Minewache-Team/timecode-obs/actions) tab.

## Installation

### Windows

**Installer (recommended)**
Download the latest `.exe` from the [Releases](https://github.com/Minewache-Team/timecode-obs/releases) page, run it, and restart OBS Studio.

**Manual install**
Download the `.zip` from the [Releases](https://github.com/Minewache-Team/timecode-obs/releases) page and extract it to:

```text
C:\ProgramData\obs-studio\plugins\obs-ltc-timecode\

```

OBS does not load plugins from the AppData folder, so make sure you use ProgramData.

### Linux

**Install script**

```bash
chmod +x install.sh && ./install.sh

```

**.deb package**

```bash
sudo dpkg -i obs-ltc-timecode_*.deb

```

**Manual install**

```bash
mkdir -p ~/.config/obs-studio/plugins/obs-ltc-timecode/bin/64bit
mkdir -p ~/.config/obs-studio/plugins/obs-ltc-timecode/data
cp obs-ltc-timecode.so ~/.config/obs-studio/plugins/obs-ltc-timecode/bin/64bit/
cp -r data/* ~/.config/obs-studio/plugins/obs-ltc-timecode/data/

```

To uninstall, run `./install.sh --uninstall`.

## Usage

Go to your OBS Sources, click **+**, and add **LTC Timecode Generator**. The plugin will show up in your Audio Mixer.
In your OBS recording settings, assign the LTC source to a separate audio track (like Track 4).

### Multi-camera setups

1. Install the plugin on all PCs.
2. Make sure they have internet access for NTP.
3. Add the LTC source and assign it to the same track number everywhere.
4. Set the same framerate on all machines.
5. Record.
6. Drop the files into DaVinci Resolve and sync them using the audio track.

### Settings

* **Framerate:** "Auto" reads the value from OBS.
* **NTP Server:** Defaults to `pool.ntp.org`.
* **Sync Interval:** Defaults to 5 minutes.

## Troubleshooting

**Plugin doesn't load**
Make sure you installed it to `C:\ProgramData\obs-studio\plugins\obs-ltc-timecode\` and not `%APPDATA%`. Check the OBS log file to see if it failed to load dependencies. You also need the 64-bit version of OBS.

**NTP sync fails**
NTP uses UDP port 123. If your network blocks it, the plugin will just use your local system clock instead.

**Timecode drift**
Check the plugin properties to ensure NTP sync is actually working. You can also try lowering the sync interval to 1 minute.

**DaVinci Resolve won't read it**
Make sure you are recording to MKV or MOV so you have multiple audio tracks. The LTC audio must be isolated on its own track without any game or mic audio mixed in.

## Building

### Prerequisites

* CMake 3.28+
* Windows: Visual Studio 2022
* Linux: `build-essential` and `ninja-build`

### Build instructions

```bash
git clone --recursive https://github.com/Minewache-Team/timecode-obs.git
cd timecode-obs

```

**Windows:**

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64

```

**Linux:**

```bash
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64

```

## Technical details

* Audio format: 48 kHz, mono, float32, -12 dBFS
* LTC encoding: libltc (LGPL-3.0)
* NTP protocol: SNTPv4 (RFC 4330)

## License

GPL-2.0-or-later. Uses dynamically linked libltc (LGPL-3.0).

## Background

This plugin was born out of frustration. While filming our Minecraft police series *Die Minewache*, we constantly struggled with synchronizing 9+ perspectives in DaVinci Resolve and other NLEs. We had been dealing with these exact problems since 2022, and the idea for this plugin had been floating around the whole time, but we kept procrastinating. The tipping point came when audio sync issues piled up because not everyone on the team recorded with 100% consistent settings.

By the time we finally started, AI coding agents had become powerful enough to act as a real development multiplier. We built a framework where an AI agent helped us ship the plugin significantly faster than we could have on our own.

A note on scope: This plugin was originally planned as an internal tool, but was implemented as open source from the start, and the repository went public with the first binary release. Because of our specific workflow, you will notice Minewache-specific customizations throughout the project, such as auto-setup templates, pre-configured audio track routing, and German-language dialogs. The current version prioritizes fast onboarding for our team over general usability.

A more generic version is planned, but there is no timeline for it yet. If you do not want to wait, you are welcome to fork this repository. The Minewache-specific parts do not need to be removed for the plugin to work, but stripping them out is recommended for a cleaner general-purpose build. Here is what you can remove or adapt:

* **[MW] OBS KIT/**: The entire directory (pre-configured OBS scene collection and profile template)
* **src/auto-setup.c** and **src/auto-setup.h**: First-run auto-setup that offers to switch to the Minewache scene collection/profile
* **src/plugin-main.c**: Remove the include "auto-setup.h" and the auto_setup_init() / auto_setup_cleanup() calls
* **CMakeLists.txt**: Remove src/auto-setup.c and src/auto-setup.h from the source list
* **installer/obs-ltc-timecode.iss**: Remove the MW OBS KIT template sections and Minewache-specific post-install instructions

The core plugin features, including LTC generation, NTP sync, and timecode encoding, are completely independent of these customizations and work fine without them.

## Credits

* **Author:** Ferdmusic
* **LTC encoding:** libltc by Robin Gareus
* **Plugin template:** obs-plugintemplate by the OBS Project