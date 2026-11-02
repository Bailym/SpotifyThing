# SpotifyThing Arduino

A Raspberry Pi Pico W project that displays the currently playing Spotify track on an SH1106 OLED display.

## Hardware

- **Board:** Raspberry Pi Pico W
- **Display:** SH1106 128x64 OLED (I2C, address 0x3C)
  - SDA: GP16
  - SCL: GP17
- **Encoder:** KY-040 rotary encoder
  - CLK: GP19
  - DT: GP20
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
├── wifiManager.h/cpp     # WiFi connection
├── spotify.h/cpp         # Spotify API client (fetch, token refresh, play/pause, skip, volume)
├── userControls.h/cpp    # Encoder input, button gesture detection (single/double press), rotation
├── credentials.h/cpp     # Credential storage in the EEPROM flash sector (CRC-checked record)
└── usbConfig.h/cpp       # USB drive, config.txt import and validation, status.txt
lib/
└── ky-040/               # KY-040 encoder driver (polling via repeating timer)
```

## Features

- Connects to WiFi on boot and disables power saving mode for consistent latency
- Fetches the currently playing track from the Spotify API every 5 seconds
- Only updates the display when the track ID changes — avoids unnecessary redraws and scroll resets
- Retains the last track on screen when nothing is playing
- Acquires the Spotify access token at boot from the stored refresh token (`acquireInitialAccessToken()`), and automatically refreshes it on 401 responses, retrying the request at most once (`_doFetch()` / `_doSkip()` take a `retried` flag); `refreshAccessToken()` rejects a response with a missing or empty `access_token`
- Backs off on 429 responses with an exponential rate-limit window — no Spotify calls are made while it is open (see [Rate Limit Handling](#rate-limit-handling))
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
| Schedules fetch every 5 s via `requestFetch()` | Executes `_doSetVolume()` after 300 ms debounce |
| | Sleeps 600 ms for skip propagation (free on Core 1) |

### Communication

**Core 0 → Core 1 (command):** `volatile SpotifyCommand _pendingCommand` + `volatile bool _commandPending`. Core 0 sets these and returns immediately; Core 1 reads them in `tickCore1()`.

**Core 1 → Core 0 (result):** `SpotifyResult _pendingResult` protected by `mutex_t _mutex` + `volatile bool _resultReady`. Core 1 locks the mutex, writes the result struct, sets the flag, and unlocks. Core 0 checks the flag in `applyPendingResult()`, locks, copies the struct, clears the flag, unlocks, then drives all display calls.

### Key constraints

- **WiFi must be initialised on Core 0** — `wifiConnect()` stays in `setup()`. HTTP requests made after init are safe from Core 1.
- **`SpotifyResult` uses `char[]` buffers** (not `String`) to avoid heap allocation races across cores.
- **Display functions are only ever called from Core 0** — Core 1 only writes data; Core 0 applies it.

## Device Configuration

Credentials are not compiled in. With the firmware running, create `config.txt` in the root of the Pico's USB drive, then eject it. See `config.txt.example`:

```
WIFI_SSID=your-ssid
WIFI_PASS=your-password
SPOTIFY_CLIENT_ID=from Spotify Developer Dashboard
SPOTIFY_CLIENT_SECRET=from Spotify Developer Dashboard
SPOTIFY_REFRESH_TOKEN=from get_spotify_token.py
```

Include only the keys being changed. The access token is not configured; the device fetches it at boot. The README has the user-facing steps, format and length rules.

## USB Config Design

`board_build.filesystem_size = 256k` in `platformio.ini` reserves the USB drive region. `usbConfig.h/cpp` owns the drive (FatFS + FatFSUSB, about 240 KB usable); `credentials.h/cpp` owns the stored record.

The unplug callback only sets a flag. `usbConfigTick()` on Core 0 then:

1. Refuses host access through `driveReady`
2. Reads `config.txt` and merges it line by line into a copy of the current credentials — any bad line rejects the whole file, so nothing changes
3. Commits the merged record, or only the rejection reason, to EEPROM
4. Shows `Config saved` or `Config rejected`, then `Restarting...`
5. Raw-erases the whole drive region, whatever the outcome, and calls `rp2040.reboot()`

`usbConfigInit()` runs the same import at boot before USB starts, so a file that survived a pulled cable is still imported. The next boot re-formats the drive and writes `status.txt`, only if its content changed, to limit flash wear. `status.txt` never contains secret values.

### Design constraints

- **Reboot instead of applying live** — avoids syncing credentials to Core 1 and resetting WiFi, the access token and the drive state.
- **Raw erase instead of deleting the file** — a FAT delete or `FatFS.format()` can leave the secret readable in flash.
- **Plain text in the EEPROM sector** — credentials live in the 4 KB EEPROM flash sector, outside the drive region, so they are not readable through the drive and survive firmware uploads. Anyone with USB access can still read flash using picotool. Encrypting would not help: the RP2040 has no secure storage, so the key would sit in the same flash.
- **Long WiFi retries delay the import** — `wifiConnect()` blocks Core 0, so an import can wait up to about 9 s.
- **Unconfigured states stay idle** — with no WiFi set, the OLED shows `Setup needed` / `Add config.txt to USB`. With Spotify keys missing, it shows `Spotify not set up` / `Add config.txt to USB`. The drive and display keep working.

## Flashing

Run `pio run -t upload --upload-port COMx`, where `COMx` is the Pico's serial port. A running firmware reboots itself into BOOTSEL, so BOOTSEL is only needed for the first flash or a crashed device (hold BOOTSEL while plugging in USB). The `upload_port = D:\` in `platformio.ini` is stale, so use the command-line port.

## Possible Future Features

**Quick wins**
- **Skip previous** — add a triple-press or long-press gesture to call `POST /v1/me/player/previous`; the double-press detection pattern in `userControls.cpp` can be extended
- **Like current track** — long-press the encoder button to call `PUT /v1/me/tracks?ids=...` with the current track ID; show a brief confirmation on the display
- **Show playback device name** — `item.device.name` is available in the API response; could display briefly on track change or as a status line

**Medium effort**
- **Display brightness scheduling** — `display.setContrast()` (U8g2) combined with NTP time to dim the screen at night
- **Album name** — rotate between artist, album, and track on a timer using the existing scroll infrastructure; requires adding `item.album.name` to the JSON filter
- **Middleware server** — offload OAuth token management and Spotify API calls to a hosted server; Pico calls one simple endpoint and gets back artist/track, removing the client secret from the firmware
- **Seek bar interaction** — hold the encoder button and rotate to seek within the track; call `PUT /v1/me/player/seek?position_ms=N` and update the progress bar immediately
- **Shuffle / repeat toggle** — display current shuffle state (from `shuffle_state` in the API response) and toggle it with a gesture; requires adding the field to the JSON filter and a `PUT /v1/me/player/shuffle` call

**More involved**
- **Screensaver** — blank the screen after inactivity with `display.setPowerSave(1)` (U8g2) and wake on track change
- **OTA firmware updates** — the earlephilhower core has built-in `ArduinoOTA` support for flashing over WiFi
- **Playlist/context display** — show the name of the currently playing playlist or album context; requires `context.uri` and a second API call to resolve the name
- **Multi-device switching** — fetch available devices via `GET /v1/me/player/devices` and allow switching the active device from the encoder menu

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

## Rate Limit Handling

A 429 from any Spotify call routes into `handleRateLimit()`, which opens a backoff window:

1. Honours the `Retry-After` header when present, otherwise falls back to `RATE_LIMIT_DEFAULT_S`
2. Takes the larger of that value and the current backoff, then clamps it to `RATE_LIMIT_MAX_BACKOFF_S` — a hostile or broken `Retry-After` cannot park the device indefinitely
3. Doubles the next backoff on each consecutive 429, up to the same ceiling
4. Publishes an error result, so the display shows `Spotify error` / `Rate limited` for the duration of the window
5. `clearRateLimitBackoff()` resets the backoff to `RATE_LIMIT_DEFAULT_S` on any successful request — a 200/204 fetch, a successful play/pause toggle, or a successful volume PUT

While a window is open, `tickCore1()` returns early, so **no** Spotify HTTP calls of any kind are made on Core 1.

Controlled by constants at the top of `spotify.cpp`:

| Constant | Default | Description |
|---|---|---|
| `RATE_LIMIT_DEFAULT_S` | 30 | Backoff used when `Retry-After` is absent, and the value the backoff resets to on success |
| `RATE_LIMIT_MAX_BACKOFF_S` | 300 | Ceiling on any single backoff window, including a `Retry-After` value |

### Known limitation

Commands issued during a backoff window are **silently discarded with no feedback**. The backoff gate sits ahead of command dispatch in `tickCore1()`, and `_pendingCommand` is a single slot that the periodic fetch overwrites every 5 s — so a button press or volume change made while rate limited is lost before Core 1 ever sees it. Not yet addressed.
