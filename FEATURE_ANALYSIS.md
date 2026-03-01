# Feature-Analyse: Was dem Plugin noch fehlt

> Erstellt: 2026-03-01
> Basis: Analyse des aktuellen Plugin-Stands (alle Epics 1–8 fertig)

---

## Aktueller Stand (Was das Plugin bereits kann)

| Feature | Status |
|---------|--------|
| NTP-synchronisierter LTC-Audio-Output | Fertig |
| SMPTE Timecodes (24/25/29.97df/30/50/60 fps) | Fertig |
| Auto-Framerate-Erkennung aus OBS | Fertig |
| Drift-Erkennung & Re-Sync | Fertig |
| Properties UI (Framerate, NTP-Server, Sync-Intervall) | Fertig |
| Minewache Scene-Collection-Template (Track 3) | Fertig |
| Auto-Setup Dialog beim ersten Start | Fertig |
| Install-Skripte (PowerShell, Bash, Inno Setup) | Fertig |

---

## Vorgeschlagene neue Features

### Priorität 1: Hoher Nutzen, direkt umsetzbar

---

#### 1. LTC User Bits — Datum & Kamera-ID einbrennen

**Was:** Das LTC-Format hat 32 sogenannte "User Bits" (Binary Groups), die **zusätzlich zum Timecode** in jedem einzelnen Audio-Frame mitcodiert werden. Diese werden von DaVinci Resolve, Avid, Premiere Pro etc. automatisch ausgelesen.

**Was man reinschreiben kann:**
- **Datum (SMPTE 12M / 309M):** Tag, Monat, Jahr + Zeitzone — direkt im LTC-Standard definiert
- **Kamera-/Reel-ID:** z.B. "CAM A" = `0x01`, "CAM B" = `0x02` etc. (4-stellige Hex-Zahl, frei belegbar)
- **Custom Identifier:** z.B. Produktions-ID, Aufnahme-Nummer

**Warum das wichtig ist:**
- Bei Multi-Cam-Setups weiß DaVinci Resolve sofort, von welcher Kamera das Material kommt
- Das Datum ist direkt im LTC-Stream encoded — kein separates Metadaten-File nötig
- Professionelle LTC-Hardware (Tentacle Sync, Ambient, Denecke) macht das standardmäßig

**Technische Umsetzbarkeit:**
- libltc unterstützt User Bits bereits: `ltc_encoder_set_user_bits()` API ist vorhanden
- `LTC_USE_DATE` Flag wird schon bei `ltc_encoder_create()` gesetzt
- `ltc_time_to_frame()` kann Datum automatisch in User Bits schreiben
- Aufwand: ~2–3 Stunden (Wrapper erweitern + UI-Felder)

**Neue UI-Felder:**
- Checkbox: "Datum in LTC einbetten" (Default: An)
- Textfeld: "Kamera-ID" (z.B. "A", "B", "1", "2" — wird als Hex in User Bits geschrieben)

---

#### 2. Visuelles Timecode-Overlay (Window Burn)

**Was:** Ein Text-Overlay direkt im Videobild, das den aktuellen Timecode anzeigt — ähnlich wie bei professionellen Kameras oder einem "Window Burn" auf Dailies.

**Anzeige-Format:**
```
┌──────────────────────────────────┐
│                                  │
│                                  │
│                                  │
│                                  │
│          14:30:22:15             │
│   CAM A  2026-03-01  25fps      │
│                                  │
└──────────────────────────────────┘
```

**Warum das wichtig ist:**
- Sofortige visuelle Referenz beim Review
- Kein Decoder nötig — Timecode ist direkt sichtbar
- Unverzichtbar für: Client-Reviews, Dailies, Proxy-Schnitt, QC
- Kann optional nur beim Recording oder auch beim Streaming angezeigt werden

**Technische Umsetzung — Zwei Varianten:**

| Variante | Beschreibung | Aufwand |
|----------|-------------|---------|
| A) Lua-Script + Text-Source | OBS Lua-Script updatet eine GDI+ Text-Source jeden Frame | Einfach (~4h) |
| B) Nativer Video-Filter | OBS `video_render` Callback mit FreeType2 Text-Rendering | Mittel (~2 Tage) |

**Empfehlung:** Variante A als Sofortlösung (Lua-Script wird mitgeliefert), Variante B als langfristiges Ziel (bessere Performance, kein separater Source nötig).

**Konfigurierbare Optionen:**
- Position (9 Ankerpunkte: oben-links bis unten-rechts)
- Schriftgröße & Farbe
- Hintergrund (halbtransparenter Kasten oder ohne)
- Format: nur TC, TC + Datum, TC + Kamera-ID + Datum + FPS
- Nur bei Recording / Nur bei Streaming / Immer

---

#### 3. Aufnahme-Metadaten Sidecar-Datei

**Was:** Beim Start jeder Aufnahme wird automatisch eine `.json`-Datei neben dem Recording erstellt (z.B. `Recording_2026-03-01_14-30.json`).

**Inhalt:**
```json
{
  "recording": {
    "filename": "Recording_2026-03-01_14-30.mkv",
    "start_time_utc": "2026-03-01T14:30:22.458Z",
    "start_timecode": "14:30:22:11",
    "end_time_utc": "2026-03-01T15:45:10.220Z",
    "end_timecode": "15:45:10:05",
    "duration_seconds": 4487.762
  },
  "timecode": {
    "framerate": "25fps",
    "framerate_mode": "auto",
    "drop_frame": false,
    "ntp_server": "pool.ntp.org",
    "ntp_synced": true,
    "ntp_offset_ms": -12,
    "ntp_roundtrip_ms": 45
  },
  "source": {
    "camera_id": "CAM-A",
    "scene_collection": "Minewache",
    "obs_version": "32.1.0"
  },
  "video": {
    "resolution": "1920x1080",
    "output_fps": 25,
    "encoder": "x264",
    "bitrate_kbps": 6000
  },
  "audio": {
    "sample_rate": 48000,
    "ltc_track": 3,
    "ltc_amplitude_dbfs": -12
  },
  "plugin_version": "0.1.0"
}
```

**Warum das wichtig ist:**
- Automatische Dokumentation jeder Aufnahme
- NTP-Sync-Qualität ist nachvollziehbar (war die Uhr korrekt?)
- Post-Production-Tools können das maschinenlesbar auswerten
- Bei Multi-Cam: schneller Überblick über alle Kameras und deren Sync-Status

**Aufwand:** ~4–6 Stunden (Frontend-API für Recording-Start/Stop Events + JSON-Writer)

---

### Priorität 2: Sehr nützlich, mittlerer Aufwand

---

#### 4. Timecode-Dock / Live-Anzeige im OBS-Fenster

**Was:** Ein OBS-Dock-Panel (wie Chat oder Stats), das eine große Timecode-Anzeige zeigt.

**Anzeige:**
```
┌─────────────────────────┐
│  LTC TIMECODE           │
│                         │
│   14:30:22:15           │
│                         │
│  NTP: Synced (+12ms)    │
│  FPS: 25                │
│  REC: 00:45:12          │
│  CAM: A                 │
└─────────────────────────┘
```

**Warum:**
- Der Operator sieht den Timecode auf einen Blick
- NTP-Sync-Status sofort erkennbar (grün/rot)
- Recording-Dauer live sichtbar
- Besonders nützlich bei Live-Events und Multi-Cam-Setups

**Aufwand:** ~1–2 Tage (erfordert `obs-frontend-api` und Qt-UI, da `ENABLE_QT=OFF` aktuell)

---

#### 5. HTTP/HTTPS Time-Fallback (für restriktive Netzwerke)

**Was:** Wenn NTP (UDP Port 123) blockiert ist (Firmennetzwerke, Schulen, Hotels), als Fallback einen HTTP-basierten Zeitdienst nutzen.

**Optionen:**
- `worldtimeapi.org/api/ip` (REST API, JSON)
- HTTP `Date`-Header von einem beliebigen HTTPS-Server
- Eigener Server (konfigurierbar)

**Warum:**
- NTP ist in vielen Netzwerken geblockt
- HTTP/HTTPS ist fast überall erlaubt
- Genauigkeit ~100–500ms (reicht für LTC, da Drift-Korrektur bereits vorhanden)
- Aktuell fällt das Plugin einfach auf die lokale Uhr zurück — das kann Minuten daneben liegen

**Aufwand:** ~4–6 Stunden (HTTP-Client, JSON-Parser, Fallback-Logik)

---

#### 6. Audio-Track-Auswahl im Plugin

**Was:** Direkt in den Plugin-Einstellungen auswählen, auf welchem Audio-Track der LTC-Output landen soll (statt es manuell im OBS-Mixer zu konfigurieren).

**Aktuelle Situation:** LTC geht auf alle Tracks, und man muss manuell im OBS-Mixer auf "Track 3 only" umstellen (mixers=4 ist nur im Template vorkonfiguriert).

**Warum:** Weniger manuelle Konfiguration, weniger Fehlermöglichkeiten.

**Aufwand:** ~2 Stunden (OBS `obs_source_set_audio_mixers()` API)

---

### Priorität 3: Nice-to-have / Zukunftsfeatures

---

#### 7. Timecode-Log / EDL-Export

**Was:** Während der Aufnahme werden alle relevanten Events mit Timecode geloggt:
- Recording Start/Stop
- Scene-Wechsel
- Hotkey-gesteuerte Marker (z.B. "Markiere wichtige Stelle")

Export als EDL (Edit Decision List) oder CSV für Import in NLEs.

**Warum:** Beschleunigt den Schnitt enorm — der Editor weiß sofort, wo die wichtigen Stellen sind.

**Aufwand:** ~1–2 Tage

---

#### 8. MIDI Timecode (MTC) Output

**Was:** Zusätzlich zum LTC-Audio auch MIDI Timecode über einen virtuellen MIDI-Port ausgeben.

**Warum:** Synchronisation mit Audio-DAWs (Pro Tools, Logic Pro, Ableton Live, Reaper). Relevant für Musik-Livestreams und Konzertmitschnitte.

**Aufwand:** ~2–3 Tage (plattformspezifische MIDI-APIs: Windows MIDI, ALSA/JACK)

---

#### 9. Timecode-Offset / Custom Start-Time

**Was:** Statt immer die Tageszeit als Timecode zu verwenden, einen festen Start-Timecode setzen (z.B. 01:00:00:00 für Reel 1, 02:00:00:00 für Reel 2).

**Warum:**
- Standard-Workflow in der Filmproduktion (jede Rolle beginnt bei einer vollen Stunde)
- Vermeidet Verwirrung bei Aufnahmen über Mitternacht
- Ermöglicht Recording-basiertes statt Tageszeit-basiertes Timecoding

**Aufwand:** ~2–4 Stunden

---

#### 10. NDI Timecode Embedding

**Was:** Wenn OBS mit NDI-Output arbeitet, den Timecode direkt in die NDI-Stream-Metadaten einbetten (NDI unterstützt ein eigenes Timecode-Feld).

**Warum:** Bei NDI-basierten Produktionen (Studio-Setup, Kirchen, Events) wird der Timecode direkt im Stream transportiert — kein separater Audio-Track nötig.

**Aufwand:** ~1 Tag (NDI SDK Integration)

---

## Zusammenfassung: Empfohlene Reihenfolge

| Prio | Feature | Nutzen | Aufwand | Empfehlung |
|------|---------|--------|---------|------------|
| 1 | LTC User Bits (Datum + Kamera-ID) | Sehr hoch | Klein (~3h) | **Sofort umsetzen** |
| 1 | Visuelles Timecode-Overlay | Sehr hoch | Klein–Mittel | **Lua-Script als Sofortlösung** |
| 1 | Metadaten Sidecar-Datei | Hoch | Mittel (~6h) | **Für v0.2.0** |
| 2 | Timecode-Dock | Mittel–Hoch | Mittel | Für v0.3.0 |
| 2 | HTTP Time-Fallback | Mittel | Mittel | Für v0.2.0 |
| 2 | Audio-Track-Auswahl | Mittel | Klein (~2h) | Für v0.2.0 |
| 3 | Timecode-Log / EDL | Mittel | Mittel | Zukunft |
| 3 | MIDI Timecode | Nische | Hoch | Zukunft |
| 3 | Custom Start-Timecode | Mittel | Klein | Zukunft |
| 3 | NDI Timecode | Nische | Mittel | Zukunft |

---

## Technische Notizen

### LTC User Bits — libltc API

Die bestehende libltc-Integration unterstützt User Bits bereits:

```c
// Bereits im Code: LTC_USE_DATE wird bei ltc_encoder_create() gesetzt
w->encoder = ltc_encoder_create(sample_rate, fps_rate, tv_std, LTC_USE_DATE);

// Datum setzen (in ltc_wrapper_set_timecode erweitern):
SMPTETimecode st;
st.years = 26;       // 2026
st.months = 3;       // März
st.days = 1;         // 1.
// → ltc_time_to_frame() schreibt das automatisch in die User Bits

// Custom User Bits (z.B. Kamera-ID):
ltc_encoder_set_user_bits(encoder, 0x0001); // CAM A = 0x0001
```

### Visuelles Overlay — Lua-Script Ansatz

```lua
-- Minimal-Beispiel für OBS Lua Timecode Overlay
obs.timer_add(function()
    local source = obs.obs_get_source_by_name("Timecode Display")
    if source then
        local settings = obs.obs_data_create()
        obs.obs_data_set_string(settings, "text", os.date("!%H:%M:%S") .. ":00")
        obs.obs_source_update(source, settings)
        obs.obs_data_release(settings)
        obs.obs_source_release(source)
    end
end, 33) -- ~30fps Update
```

### Metadaten Sidecar — OBS Events

```c
// Recording Start/Stop Events über obs-frontend-api:
obs_frontend_add_event_callback(on_event, ctx);

static void on_event(enum obs_frontend_event event, void *ctx) {
    if (event == OBS_FRONTEND_EVENT_RECORDING_STARTED) {
        // JSON-Sidecar schreiben
        const char *output_path = obs_frontend_get_current_record_output_path();
        write_metadata_json(output_path, ctx);
    }
    if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
        // End-Timecode + Dauer nachtragen
        update_metadata_json_end(ctx);
    }
}
```

---

## Quellen

- [LTC User Bits (Wikipedia)](https://en.wikipedia.org/wiki/Linear_timecode)
- [libltc API Dokumentation](https://x42.github.io/libltc/ltc_8h.html)
- [libltc GitHub](https://github.com/x42/libltc)
- [SMPTE Timecode Überblick](https://www.philrees.co.uk/articles/timecode.htm)
- [OBS Plugin API Dokumentation](https://docs.obsproject.com/plugins)
- [RTC Timecode Generator (OBS Lua)](https://obsproject.com/forum/resources/rtc-timecode-generator.1301/)
