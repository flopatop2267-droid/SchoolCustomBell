# School Bell — Musical Bell System for Windows

A C++ backend with a local web interface that plays music tracks as school bells on a schedule. Open the control panel in any browser on the PC.

## Features

- Automatic bells at times you set, per schedule profile (Regular Day, Shortened Day, Silence, or your own)
- Each bell plays a track from your music folder, sequentially or at random
- Configurable bell length (e.g. 15–20 s) with a smooth fade-out
- Web UI: edit schedules, upload MP3/WAV tracks, change settings
- "Test bell" and emergency "Stop" buttons

## Requirements

- Windows 10 or 11
- Visual Studio 2022 (with the "Desktop development with C++" workload) **or** MinGW-w64
- CMake 3.16 or newer
- Free third-party header files (see step 1 below)

## 1. Download the third-party libraries

Create a folder `third_party` inside the project and add these files:

| File | Where to get it |
|---|---|
| `httplib.h` | https://github.com/yhirose/cpp-httplib (file `httplib.h` in the repo root) |
| `miniaudio.h` | https://github.com/mackron/miniaudio (file `miniaudio.h` in the repo root) |
| `nlohmann/json.hpp` | https://github.com/nlohmann/json (file `single_include/nlohmann/json.hpp`), keep the `nlohmann` folder structure |

The final layout must look like this:

```
school-bell/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp
│   ├── Config.h
│   ├── Scheduler.h
│   ├── AudioPlayer.h
│   └── AudioPlayer.cpp
├── web/
│   ├── index.html
│   ├── app.js
│   └── style.css
└── third_party/
    ├── httplib.h
    ├── miniaudio.h
    └── nlohmann/
        └── json.hpp
```

## 2. Build

Open a terminal (PowerShell or Command Prompt) in the `school-bell` folder:

```bat
cmake -B build
cmake --build build --config Release
```

The build copies the `web` folder next to the executable automatically. The result is `build\Release\school_bell.exe` (Visual Studio) or `build\school_bell.exe` (MinGW).

## 3. Prepare the music folder

Create a folder named `music` next to the executable:

```
build\Release\
├── school_bell.exe
├── web\
├── data\        (created on first run)
└── music\       ← put your MP3 and WAV bell tracks here
```

You can also upload tracks from the web UI, or change the folder path in the Settings card.

## 4. Run

```bat
cd build\Release
school_bell.exe
```

Optional: pass a different port, e.g. `school_bell.exe 9000`.

Then open **http://localhost:8080** in a browser.

Keep the console window open while the bell system runs. Closing it stops the server.

## 5. Using the web interface

**Status card**
- Shows the server clock, what is playing, the active schedule profile, and the track count.
- **Тестовый звонок** plays the next track now, using the saved settings.
- **Стоп** stops the current bell immediately.

**Расписание (Schedule) card**
- Pick a profile from the dropdown and edit its bell times. Untick a bell to disable it without deleting it.
- **+ Звонок** adds a bell. **✕** deletes one.
- **+ Режим** creates a new profile, such as a shortened day.
- **Сделать активным** makes the selected profile the active one (it saves first).
- **Сохранить** saves the schedule.

To make a holiday silence, create a profile with no bells, or select the existing "Silence" profile and make it active.

**Музыка (Music) card**
- Upload MP3 or WAV files. Several can be selected at once.
- The list shows the tracks in the music folder.

**Настройки (Settings) card**
- **Папка с треками**: path to the music folder.
- **Длительность звонка**: how long each bell plays, in seconds.
- **Затухание**: fade-out length at the end of each bell, in seconds (must not exceed the duration).
- **Громкость**: volume, 0–100%.
- **Порядок треков**: "По порядку" (in order) or "Случайно" (random).
- **Сохранить настройки** saves the settings.

Remember to save after each change. The test bell and scheduled bells use the saved settings.

## 6. Configuration file

All settings and schedules are stored in `data\config.json`. You can back it up by copying this file. It is safe to edit by hand while the server is stopped; invalid times or out-of-range values are rejected when saved from the UI.

## 7. Allow other PCs on the school network (optional)

By default the interface is only reachable from the PC it runs on. To open it from other computers on the local network:

1. In `src/main.cpp`, change `svr.listen("127.0.0.1", port);` to `svr.listen("0.0.0.0", port);`.
2. Rebuild.
3. Allow the port through Windows Firewall when prompted, or add a rule for TCP port 8080.
4. Open `http://<PC-IP-address>:8080` from another computer (find the IP with `ipconfig`).

The interface has no password, so only enable this on a trusted network.

## 8. Run automatically at startup

1. Put a shortcut to `school_bell.exe` in the Startup folder (`Win + R` → `shell:startup`), with "Start in" set to the executable's folder so it finds `web` and `data`.
2. Or install it as a Windows service with NSSM (https://nssm.cc).

Set the PC clock to Moscow time (UTC+3) and enable automatic time sync.

## Troubleshooting

| Problem | Solution |
|---|---|
| "Audio device initialisation failed" | Check that a playback device is set as default in Windows sound settings. |
| "No MP3/WAV tracks found" | Check that the folder path in Settings is correct and contains `.mp3` or `.wav` files. |
| Page does not load | Make sure the `web` folder sits next to `school_bell.exe` and the console shows the server running. |
| Bells ring at the wrong time | Check the PC clock and time zone. The scheduler uses local time. |
| Cyrillic file names not playing | Use the current build; it opens files with wide-character paths. |
| Port 8080 already in use | Start with another port, e.g. `school_bell.exe 9000`. |
| Upload fails with "in use" | Close any program that has the file open. |
| No sound from amplifier | Check the USB audio interface is the default device and the amplifier input and zone are correct. |

## Limitations

- Uses 1-second polling for status updates (no WebSocket).
- Bells play one at a time; a new bell interrupts the one still playing.
- No authentication: keep the server on localhost or a trusted network.
- The scheduler checks once per minute and does not support per-weekday schedules yet; add separate profiles and switch them manually, or extend `Config.h` and `onMinute` in `main.cpp`.
- This system is not a fire-alarm or evacuation system (СОУЭ). Those must remain separate, certified systems.
