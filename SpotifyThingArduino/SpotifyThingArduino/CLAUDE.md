# SpotifyThing Arduino

A Raspberry Pi Pico W project that displays the currently playing Spotify track on an SH1106 OLED display.

## Hardware

- **Board:** Raspberry Pi Pico W
- **Display:** SH1106 128x64 OLED (I2C, address 0x3C)
  - SDA: GP16
  - SCL: GP17

## Platform

- **Framework:** Arduino via [earlephilhower/arduino-pico](https://github.com/earlephilhower/arduino-pico) (maxgerhardt platform wrapper)
- **Tooling:** PlatformIO
- **Board target:** `rpipicow`

## Project Structure

```
src/
├── main.cpp          # setup() and loop() only
├── display.h/cpp     # SH1106 init, message rendering, scroll logic
├── wifi_manager.h/cpp # WiFi connection
├── spotify.h/cpp     # Spotify API client (fetch, token refresh)
└── secrets.h         # Credentials (gitignored — copy from secrets.h.example)
```

## Features

- Connects to WiFi on boot and disables power saving mode for consistent latency
- Fetches the currently playing track from the Spotify API every 3 seconds
- Only updates the display when the track ID changes — avoids unnecessary redraws and scroll resets
- Retains the last track on screen when nothing is playing
- Automatically refreshes the Spotify access token on 401 responses
- Horizontal scrolling for artist/track names that exceed the display width, with configurable pause at each end

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

## Display Scroll Behaviour

Controlled by constants at the top of `display.cpp`:

| Constant | Default | Description |
|---|---|---|
| `SCROLL_SPEED_MS` | 40 | Milliseconds per pixel scrolled |
| `SCROLL_PAUSE` | 60 frames | Pause duration at start and end of scroll (~2.4s) |
