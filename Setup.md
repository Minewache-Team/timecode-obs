# Setup-Anleitung: obs-ltc-timecode

> Vom leeren Ordner bis zum fertigen, getesteten OBS-Plugin.
> Zielgruppe: Entwickler mit grundlegenden C/C++ und Git-Kenntnissen.

---

## Voraussetzungen

### Software installieren

| Tool | Windows | Linux (Ubuntu 24.04) | Zweck |
|------|---------|----------------------|-------|
| **Git** | [git-scm.com](https://git-scm.com) | `sudo apt install git` | Versionskontrolle |
| **CMake 3.28+** | [cmake.org](https://cmake.org/download/) | `sudo apt install cmake` | Build-System |
| **CLion** (empfohlen) | [jetbrains.com/clion](https://www.jetbrains.com/clion/) | Flatpak oder .tar.gz | IDE (kostenlos für nicht-kommerziell) |
| **Visual Studio 2022** | [visualstudio.microsoft.com](https://visualstudio.microsoft.com/) | — | Compiler (Windows) |
| **GCC/Clang** | — | `sudo apt install build-essential` | Compiler (Linux) |
| **Ninja** | Via VS Installer oder `choco install ninja` | `sudo apt install ninja-build` | Schnellerer Build |
| **OBS Studio** | [obsproject.com](https://obsproject.com) | PPA (s.u.) | Zum Testen |

### Linux: OBS PPA einrichten

```bash
sudo add-apt-repository --yes ppa:obsproject/obs-studio
sudo apt-get update
sudo apt-get install obs-studio libobs-dev
```

### Windows: Visual Studio Workload

Im Visual Studio Installer → "Desktopentwicklung mit C++" auswählen.
Sicherstellen, dass das **Windows 11 SDK (10.0.22621+)** installiert ist.

---

## Schritt 1: Repository erstellen

### Option A: Über GitHub (empfohlen)

1. Gehe zu [github.com/obsproject/obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)
2. Klicke auf **"Use this template"** → **"Create a new repository"**
3. Name: `obs-ltc-timecode`
4. Klone dein neues Repo:

```bash
git clone --recursive https://github.com/DEIN-USER/obs-ltc-timecode.git
cd obs-ltc-timecode
```

### Option B: Lokal von Grund auf

```bash
mkdir obs-ltc-timecode && cd obs-ltc-timecode
git init
```

> **Hinweis:** Option A ist empfohlen, da das Template CI/CD, CMake-Presets und
> Build-Skripte mitbringt. Bei Option B musst du die Dateien aus diesem
> Projektpaket manuell einfügen.

---

## Schritt 2: libltc als Submodule hinzufügen

```bash
git submodule add https://github.com/x42/libltc.git deps/libltc
git submodule update --init --recursive
```

Verifizieren:
```bash
ls deps/libltc/src/ltc.h  # Muss existieren
```

---

## Schritt 3: Projektdateien einfügen

Kopiere alle Dateien aus diesem Projektpaket in dein Repo.
Die Struktur sollte danach so aussehen:

```
obs-ltc-timecode/
├── .github/
│   └── workflows/
│       └── build.yml              # CI/CD Pipeline
├── cmake/
│   └── BuildLibLTC.cmake          # libltc Build-Konfiguration
├── data/
│   └── locale/
│       └── en-US.ini              # UI-Strings
├── deps/
│   └── libltc/                    # Git Submodule
├── docs/
│   ├── SETUP.md                   # ← Diese Datei
│   ├── PROJECT.md                 # Externes Gehirn (Agent State)
│   └── SYSTEM_PROMPT.md           # KI-Agent Systemprompt
├── src/
│   ├── plugin-main.c              # OBS Plugin Entry Point
│   ├── ltc-source.h               # Audio Source Header
│   ├── ltc-source.c               # Audio Source Implementation
│   ├── ntp-client.h               # NTP Sync Header
│   ├── ntp-client.c               # NTP Sync Implementation
│   ├── timecode.h                 # SMPTE Timecode Header
│   ├── timecode.c                 # SMPTE Timecode Implementation
│   ├── ltc-encoder-wrapper.h      # libltc Wrapper Header
│   └── ltc-encoder-wrapper.c      # libltc Wrapper Implementation
├── tests/
│   ├── CMakeLists.txt             # Test Build Config
│   ├── test-timecode.cpp          # Timecode Unit Tests
│   ├── test-ntp-offset.cpp        # NTP Offset Tests
│   └── test-ltc-roundtrip.cpp     # Encode→Decode Roundtrip Tests
├── CMakeLists.txt                 # Root Build File
├── CMakePresets.json              # Build Presets
├── buildspec.json                 # OBS SDK Versionen
├── .gitignore
├── .gitmodules
├── LICENSE
└── README.md
```

---

## Schritt 4: Erstes Build

### Windows (PowerShell)

```powershell
# Konfigurieren
cmake --preset windows-x64

# Bauen
cmake --build --preset windows-x64

# Tests ausführen
cd build_x64
ctest --output-on-failure
cd ..
```

### Linux

```bash
# Konfigurieren
cmake --preset ubuntu-x86_64

# Bauen
cmake --build --preset ubuntu-x86_64

# Tests ausführen
cd build_x86_64
ctest --output-on-failure
cd ..
```

### Erwartetes Ergebnis nach dem ersten Build

Das Skeleton-Build sollte erfolgreich durchlaufen und:
- Eine Plugin-Binary erzeugen (`.dll` / `.so`)
- Alle Tests bestehen (anfangs nur Skeleton-Tests)
- Keine Warnungen oder Fehler produzieren

---

## Schritt 5: IDE einrichten

### CLion (empfohlen)

1. **Öffne CLion** → "Open" → wähle den `obs-ltc-timecode` Ordner
2. CLion erkennt automatisch `CMakeLists.txt` und `CMakePresets.json`
3. **Preset auswählen:** Unten in der Toolbar → Preset passend zum OS wählen
4. **Build:** `Ctrl+F9` (oder ▶ Button)
5. **Tests:** Rechtsklick auf `tests/` → "Run All Tests"
6. **Debugging:**
   - Run → Edit Configurations → "+" → CMake Application
   - Executable: Pfad zur `obs64.exe` / `obs` Binary
   - Working Directory: OBS Installationsordner
   - Plugin-Binary in den OBS Plugin-Ordner kopieren (oder Symlink)

### VS Code (Alternative)

Extensions installieren:
- **clangd** (llvm-vs-code-extensions.vscode-clangd)
- **CMake Tools** (ms-vscode.cmake-tools)
- **C/C++** (ms-vscode.cpptools) — nur für Debugging

```json
// .vscode/settings.json
{
    "cmake.useCMakePresets": "always",
    "clangd.arguments": [
        "--compile-commands-dir=${workspaceFolder}/build_x64",
        "--header-insertion=never"
    ],
    "C_Cpp.intelliSenseEngine": "disabled"
}
```

---

## Schritt 6: Plugin in OBS testen

### Windows

```powershell
# Plugin-Binary kopieren
$OBS_PLUGINS = "$env:APPDATA\obs-studio\plugins\obs-ltc-timecode"
New-Item -ItemType Directory -Force -Path "$OBS_PLUGINS\bin\64bit"
New-Item -ItemType Directory -Force -Path "$OBS_PLUGINS\data"

Copy-Item "build_x64\Release\obs-ltc-timecode.dll" "$OBS_PLUGINS\bin\64bit\"
Copy-Item -Recurse "data\*" "$OBS_PLUGINS\data\"
```

### Linux

```bash
# Plugin-Binary kopieren
OBS_PLUGINS=~/.config/obs-studio/plugins/obs-ltc-timecode
mkdir -p "$OBS_PLUGINS/bin/64bit"
mkdir -p "$OBS_PLUGINS/data"

cp build_x86_64/obs-ltc-timecode.so "$OBS_PLUGINS/bin/64bit/"
cp -r data/* "$OBS_PLUGINS/data/"
```

### In OBS verifizieren

1. OBS starten
2. **Quellen** → **"+"** → Es sollte **"LTC Timecode Generator"** in der Liste erscheinen
3. Quelle hinzufügen → Im Audio-Mixer sollte ein stiller Kanal erscheinen
4. Im Properties-Panel: NTP Status und Framerate-Auswahl prüfen

---

## Schritt 7: Mit dem KI-Agenten arbeiten

### Erstmalige Session starten

1. Kopiere den Inhalt von `docs/SYSTEM_PROMPT.md` als System-Prompt in dein KI-Tool
2. Lade `docs/PROJECT.md` als Kontext hoch (oder füge den Inhalt ein)
3. Erster Prompt:

```
Lies PROJECT.md vollständig. Identifiziere das nächste TODO-Ticket und starte damit.
```

### Nach jedem Ticket

1. Code-Output des Agenten prüfen und in die richtigen Dateien einbauen
2. Die Updates an `PROJECT.md` übernehmen (Ticket-Status, Decision Log, Session Log)
3. Bauen + Tests ausführen
4. Nächster Prompt: `"Weiter."` oder `"Ticket X hat folgendes Problem: ..."`

### Agent-Crash oder neue Session

1. Neue Session öffnen
2. Gleichen System-Prompt laden
3. **Aktuelle** `PROJECT.md` hochladen (nicht die alte!)
4. Prompt: `"Lies PROJECT.md. Wo stehen wir? Mach weiter."`

→ Der neue Agent weiß exakt, was getan wurde und was noch offen ist.

---

## Schritt 8: CI/CD (GitHub Actions)

Das Repository enthält eine `.github/workflows/build.yml` die automatisch:
- Auf jedem Push baut (Windows + Linux)
- Tests ausführt
- Bei Tag-Push (z.B. `v1.0.0`) ein GitHub Release erstellt

### Release erstellen

```bash
git tag v0.1.0
git push origin v0.1.0
```

→ GitHub Actions baut automatisch und hängt die Binaries an das Release an.

---

## Entwicklungs-Workflow Zusammenfassung

```
┌─────────────────────────────────────────────────────────────┐
│                    Entwicklungszyklus                        │
│                                                             │
│  1. PROJECT.md lesen → Nächstes Ticket identifizieren       │
│  2. KI-Agent arbeitet Ticket ab                             │
│  3. Code reviewen + einfügen                                │
│  4. cmake --build → ctest                                   │
│  5. PROJECT.md updaten (Status, Decisions, Session Log)     │
│  6. git commit + push                                       │
│  7. CI prüft automatisch                                    │
│  8. Goto 1                                                  │
│                                                             │
│  Release: git tag v1.0.0 → CI baut + published automatisch │
└─────────────────────────────────────────────────────────────┘
```

---

## Troubleshooting

| Problem | Lösung |
|---------|--------|
| `CMake Error: Could not find OBS` | Auf Windows: `buildspec.json` prüfen, OBS Version muss matchen. Auf Linux: `sudo apt install libobs-dev` |
| `libltc/src/ltc.h not found` | `git submodule update --init --recursive` |
| Plugin erscheint nicht in OBS | Binary im richtigen Pfad? `bin/64bit/` Unterordner beachten |
| NTP-Anfrage scheitert | Firewall prüft UDP Port 123. Alternativ-NTP-Server in Plugin-Settings eintragen |
| Tests finden Google Test nicht | `BUILD_TESTS=ON` in CMake setzen, Internet-Zugang für FetchContent nötig |
| CLion findet Presets nicht | CMake 3.28+ installiert? CLion → Settings → Build → CMake → Preset auswählen |