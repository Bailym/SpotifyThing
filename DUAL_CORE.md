# Dual-Core Refactor Plan

Move all Spotify HTTP requests to Core 1 so Core 0 (display + encoder) stays
responsive during blocking network calls. Currently a skip takes ~2.6s of
blocking time, during which button presses are missed and the display freezes.

---

## Architecture

```
Core 0 (main loop)          Core 1 (HTTP loop)
──────────────────          ──────────────────
displayTick()               watches commandQueue
userControlsTick()          executes HTTP requests
scheduled fetchNowPlaying() writes results back via resultQueue
reads resultQueue and
  applies display updates
```

Communication between cores uses two queues:
- `commandQueue` — Core 0 → Core 1 (e.g. TogglePlayPause, Skip, Fetch)
- `resultQueue`  — Core 1 → Core 0 (updated track info, playing state)

The earlephilhower core provides `mutex_t` and `queue_t` from the Pico SDK,
which are safe across cores.

---

## Shared State Audit

All SpotifyClient members that are currently written by HTTP methods and read
by display functions need to move out of SpotifyClient into the result struct,
so Core 1 never touches display state directly.

| Variable | Currently | After refactor |
|---|---|---|
| `_accessToken` | written by Core 0 HTTP calls | stays on Core 1 only |
| `_isPlaying` | read by display | passed via result struct |
| `_lastTrackId` | read to skip redraws | passed via result struct |
| `_idleSince` | controls clock mode | passed via result struct |

Display state (`lines[]`, `isPlaying`, `songProgressMs` etc.) stays on Core 0
and is never touched by Core 1. Core 1 only produces result data; Core 0 applies
it to the display.

---

## New Types

```cpp
// Commands queued from Core 0 → Core 1
enum class SpotifyCommand {
    Fetch,
    TogglePlayPause,
    Skip
};

// Results queued from Core 1 → Core 0
struct SpotifyResult {
    enum class Type { NowPlaying, PlayingStateChanged, Idle, Error };
    Type        type;
    bool        isPlaying;
    uint32_t    progressMs;
    uint32_t    durationMs;
    String      trackId;
    String      artist;
    String      track;
};
```

---

## Step-by-Step Implementation

### Step 1 — Add queue infrastructure to SpotifyClient

- Add `mutex_t _mutex` to SpotifyClient (or use a simple `volatile` flag for
  single-item command pending — simpler than a full queue for this use case)
- Add a `volatile SpotifyCommand _pendingCommand` and
  `volatile bool _commandPending` flag
- Add a result buffer protected by the mutex:
  `SpotifyResult _pendingResult` + `volatile bool _resultReady`
- No functional change yet — just the data structures

### Step 2 — Extract HTTP logic from SpotifyClient into private Core 1 methods

- Rename the current `fetchNowPlaying()`, `togglePlayPause()`, `skipTrack()`
  to private `_doFetch()`, `_doToggle()`, `_doSkip()`
- These become the Core 1 implementations — they still make HTTP calls and write
  to `_pendingResult` when done
- Public methods (`fetchNowPlaying()`, `togglePlayPause()`, `skipTrack()`) become
  thin wrappers that just set `_pendingCommand` and return immediately
- No behaviour change on Core 0 yet since Core 1 isn't started

### Step 3 — Add Core 1 entry point

- Add `setup1()` and `loop1()` to `main.cpp`
- `loop1()` checks `_commandPending`, calls the appropriate `_do*()` method,
  writes the result, clears the flag
- SpotifyClient needs a `void tickCore1()` method that `loop1()` calls

```cpp
// main.cpp
void setup1() { }

void loop1() {
    spotifyClient.tickCore1();
}
```

### Step 4 — Apply results on Core 0

- Add `void SpotifyClient::applyPendingResult()` — reads `_pendingResult` if
  ready and calls the appropriate display functions
- Call `applyPendingResult()` from the main `loop()` on Core 0, before
  `displayTick()`
- Remove the direct `spotifyClient.fetchNowPlaying()` call from `loop()` —
  replace with `spotifyClient.requestFetch()` which just sets the command flag

### Step 5 — Move scheduled fetch to command queue

- The 3-second poll in `loop()` currently calls `fetchNowPlaying()` directly
- Change it to `spotifyClient.requestFetch()` — sets the pending command and
  returns immediately; Core 1 picks it up and fetches in the background
- Core 0 no longer blocks on the 3-second poll at all

### Step 6 — Handle the skip propagation delay on Core 1

- The `delay(SPOTIFY_SKIP_PROPAGATION_MS)` in `skipTrack()` currently blocks
  Core 0 for 600ms
- Once on Core 1 this is free — Core 1 can `sleep_ms(600)` without affecting
  Core 0 at all

### Step 7 — Mutex audit

- Audit every access to `SpotifyResult` fields written by Core 1 and read by
  Core 0 — wrap reads/writes in mutex lock/unlock
- The `_accessToken` and other Core 1-only state needs no protection since only
  Core 1 ever touches it after the refactor
- The `_resultReady` / `_commandPending` flags can use `volatile bool` for
  single-producer/single-consumer without a full mutex, but a mutex is safer

### Step 8 — Test and verify

- Verify play/pause fires immediately with no perceivable delay
- Verify double-press skip is reliably detected even while a fetch is in progress
- Verify display keeps scrolling during HTTP calls
- Check for any race conditions: trigger rapid button presses while a fetch is
  running and confirm no crashes or missed updates

---

## What Does NOT Change

- Encoder library (`ky-040.c`) — stays on Core 0, called from `userControlsTick()`
- Display functions — all stay on Core 0, never called from Core 1
- WiFi init — stays in `setup()` on Core 0 (WiFi must be initialised on Core 0)
- `wifiConnect()` — stays on Core 0; WiFi stack is owned by Core 0

---

## Risks and Gotchas

- **WiFi must stay on Core 0's stack** — the CYW43 WiFi driver in the
  earlephilhower core is not safe to initialise on Core 1, but HTTP requests
  made after init should be fine from Core 1 with proper mutex protection
- **String is not trivially copyable** — copying `String` across cores via a
  struct needs care; consider using fixed `char[]` buffers in `SpotifyResult`
  to avoid heap allocation races
- **`_resultReady` must be checked before `applyPendingResult()` reads any
  result fields** — read the flag, then read the data, never the other way around
- **Watchdog** — if Core 1 hangs on a network timeout, Core 0 continues normally;
  consider a Core 1 watchdog or timeout guard
