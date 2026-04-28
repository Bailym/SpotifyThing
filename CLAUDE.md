# SpotifyThing Arduino

A Raspberry Pi Pico W project that displays the currently playing Spotify track on an SH1106 OLED display.

## Hardware

- **Board:** Raspberry Pi Pico W
- **Display:** SH1106 128x64 OLED (I2C, address 0x3C)
  - SDA: GP16
  - SCL: GP17
- **Encoder:** KY-040 rotary encoder
  - CLK: GP20
  - DT: GP19
  - SW: GP18

## Platform

- **Framework:** Arduino via [earlephilhower/arduino-pico](https://github.com/earlephilhower/arduino-pico) (maxgerhardt platform wrapper)
- **Tooling:** PlatformIO
- **Board target:** `rpipicow`

## Project Structure

```
src/
├── main.cpp              # setup() and loop() only
├── display.h/cpp         # SH1106 init, message rendering, scroll logic
├── wifi_manager.h/cpp    # WiFi connection
├── spotify.h/cpp         # Spotify API client (fetch, token refresh, play/pause, skip, volume)
├── userControls.h/cpp    # Encoder input, button gesture detection (single/double press), rotation
└── secrets.h             # Credentials (gitignored — copy from secrets.h.example)
lib/
└── ky-040/               # KY-040 encoder driver (polling via repeating timer)
```

## Features

- Connects to WiFi on boot and disables power saving mode for consistent latency
- Fetches the currently playing track from the Spotify API every 3 seconds
- Only updates the display when the track ID changes — avoids unnecessary redraws and scroll resets
- Retains the last track on screen when nothing is playing
- Automatically refreshes the Spotify access token on 401 responses
- Horizontal scrolling for artist/track names that exceed the display width, with configurable pause at each end
- Encoder button single press: toggle play/pause (fetches current state first to avoid stale toggle)
- Encoder button double press: skip to next track (display updates immediately after skip)
- Button debouncing (50ms) and deferred dispatch (500ms window) to distinguish single from double press
- Encoder rotation: adjust volume in 10% steps (clamped 0–100), debounced 300ms before sending the API call to avoid spamming requests while scrolling
- Volume display: an overlay showing percentage and a fill bar appears for 2 seconds on any volume change, then reverts to the normal track view
- Volume is only shown/adjusted on devices that report `supports_volume: true` in the Spotify API response

## Dual-Core Architecture

All Spotify HTTP calls run on **Core 1**, leaving **Core 0** free to handle display scrolling and button input without interruption. A skip used to block Core 0 for ~2.6s; now Core 0 never blocks on network I/O.

### Core responsibilities

| Core 0 (`loop`) | Core 1 (`loop1`) |
|---|---|
| `displayTick()` | `spotifyClient.tickCore1()` |
| `userControlsTick()` | Executes `_doFetch()`, `_doToggle()`, `_doSkip()` |
| `spotifyClient.applyPendingResult()` | Writes results to `_pendingResult` under mutex |
| Schedules fetch every 3 s via `requestFetch()` | Executes `_doSetVolume()` after 300 ms debounce |
| | Sleeps 600 ms for skip propagation (free on Core 1) |

### Communication

**Core 0 → Core 1 (command):** `volatile SpotifyCommand _pendingCommand` + `volatile bool _commandPending`. Core 0 sets these and returns immediately; Core 1 reads them in `tickCore1()`.

**Core 1 → Core 0 (result):** `SpotifyResult _pendingResult` protected by `mutex_t _mutex` + `volatile bool _resultReady`. Core 1 locks the mutex, writes the result struct, sets the flag, and unlocks. Core 0 checks the flag in `applyPendingResult()`, locks, copies the struct, clears the flag, unlocks, then drives all display calls.

### Key constraints

- **WiFi must be initialised on Core 0** — `wifiConnect()` stays in `setup()`. HTTP requests made after init are safe from Core 1.
- **`SpotifyResult` uses `char[]` buffers** (not `String`) to avoid heap allocation races across cores.
- **Display functions are only ever called from Core 0** — Core 1 only writes data; Core 0 applies it.

## Secrets Setup

Copy `src/secrets.h.example` to `src/secrets.h` and fill in your credentials:

```c
#define WIFI_SSID             "your-ssid"
#define WIFI_PASS             "your-password"
#define SPOTIFY_CLIENT_ID     "from Spotify Developer Dashboard"
#define SPOTIFY_CLIENT_SECRET "from Spotify Developer Dashboard"
#define SPOTIFY_ACCESS_TOKEN  "initial access token"
#define SPOTIFY_REFRESH_TOKEN "refresh token"
```

Client ID and secret are found in the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard). Access and refresh tokens must be obtained via the OAuth 2.0 Authorization Code flow before flashing.

## Flashing

Put the Pico W into BOOTSEL mode (hold BOOTSEL while plugging in USB), then run the PlatformIO upload task. The `upload_port` in `platformio.ini` may need updating to match the drive letter assigned by Windows.

## Possible Future Features

**Quick wins**
- **Skip previous** — add a triple-press or long-press gesture to call `POST /v1/me/player/previous`; the double-press detection pattern in `userControls.cpp` can be extended

**Medium effort**
- **Display brightness scheduling** — `display.setContrast()` (U8g2) combined with NTP time to dim the screen at night
- **Album name** — rotate between artist, album, and track on a timer using the existing scroll infrastructure; requires adding `item.album.name` to the JSON filter
- **Middleware server** — offload OAuth token management and Spotify API calls to a hosted server; Pico calls one simple endpoint and gets back artist/track, removing the client secret from the firmware

**More involved**
- **Screensaver** — blank the screen after inactivity with `display.setPowerSave(1)` (U8g2) and wake on track change
- **OTA firmware updates** — the earlephilhower core has built-in `ArduinoOTA` support for flashing over WiFi

## Display Scroll Behaviour

Controlled by constants at the top of `display.cpp`:

| Constant | Default | Description |
|---|---|---|
| `SCROLL_SPEED_MS` | 40 | Milliseconds per pixel scrolled |
| `SCROLL_PAUSE` | 60 frames | Pause duration at start and end of scroll (~2.4s) |
| `VOLUME_DISPLAY_MS` | 2000 | How long the volume overlay stays on screen after a change |

## Volume Control

Encoder rotation calls `increaseVolume()` / `decreaseVolume()` on the `SpotifyClient`, which:

1. Clamps `_targetVolume` in 10% steps (0–100)
2. Immediately updates the display overlay via `displaySetVolume()` (only if `_supportsVolume`)
3. Resets a `_volumeChangeAt` timestamp; Core 1 waits 300 ms of inactivity before firing `_doSetVolume()` — avoids spamming the API while the encoder is being turned
4. `PUT /v1/me/player/volume?volume_percent=N` is only sent to devices that report `supports_volume: true`
5. On success Core 1 publishes a `VolumeChanged` result; on the next `_doFetch()` response the synced value overwrites `_targetVolume` only if no change is in flight
