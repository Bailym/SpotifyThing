# USB Config

## Goal
Change WiFi and Spotify credentials by dropping a file on a USB drive, with no reflash and no BOOTSEL. Credentials leave the firmware image, and saved secrets can't be read back through the drive.

## Context
- `secrets.h` is compiled in at `wifiManager.cpp:18,20` and `spotify.cpp:32,72,74`. `spotify.cpp:74` builds the refresh request by string-literal concatenation, so it must change to a runtime string.
- `platformio.ini` sets no filesystem size, and the project uses no flash storage today.
- **FatFS and LittleFS can't share the default layout.** Both use the single `_FS_start`..`_FS_end` region (`FatFS.cpp:39`, `LittleFS.cpp:223`).
- **The `EEPROM` library has its own flash sector.** It uses `_EEPROM_start`, up to 4096 bytes, outside the FS region and not visible on the drive. `commit()` erases then programs, so it is not power-fail safe.
- **FatFSUSB eject handling:** the unplug callback fires on SCSI eject and flushes via `CTRL_SYNC`. Pulling the cable without ejecting skips that flush, so a just-copied file may not survive. It did survive the one Phase 1 hardware test, but that doesn't guarantee it always will, so eject remains the supported path and the boot-time check remains the fallback.
- **Flash writes stall both cores.** They run with interrupts off and the other core idled, so host writes pause Core 1 while it may be mid-TLS.
- **Deleting a file isn't enough.** FAT delete leaves the data clusters readable by raw-disk tools over USB, and `FatFS.format()` only rewrites the filesystem through the FTL.
- `test/` is empty. Per request, this work is verified on hardware, not test-first.

## Approach
Add two modules. `credentials.h/cpp` holds the versioned, CRC-checked struct in the EEPROM sector and exposes read-only accessors. `usbConfig.h/cpp` owns the drive: it starts FatFS and FatFSUSB, parses `config.txt`, and writes `status.txt`.

The unplug callback only sets a flag. `loop()` on Core 0 then does the import:

1. Refuse host access through `driveReady`.
2. Read and validate `config.txt`.
3. Merge valid values and commit them to EEPROM.
4. Raw-erase the whole FS region.
5. Show the result on the OLED, then call `rp2040.reboot()`.

The next boot re-formats the drive, writes `status.txt`, and connects with the new values. Rebooting avoids syncing credentials across cores, and it clears the stale access token, the host's cached view, and the in-memory FTL state.

`setup()` also checks for `config.txt` before starting USB, so a file that survived a pulled cable is still imported.

### `config.txt` format
- One `KEY=VALUE` per line, splitting on the first `=`. Include only the keys being changed.
- Tolerate CRLF and a UTF-8 BOM. Values are otherwise verbatim, so spaces in passwords are kept.
- Keys: `WIFI_SSID`, `WIFI_PASS`, `SPOTIFY_CLIENT_ID`, `SPOTIFY_CLIENT_SECRET`, `SPOTIFY_REFRESH_TOKEN`.
- Validation is all-or-nothing, and the file is wiped even when rejected, so secrets never stay on the drive. `status.txt` reports the failing key or line number, never the value.

## Phases

### Phase 1 — Drive appears while the firmware runs
**Status:** implemented. Mount, copy, eject callback and persistence after eject and re-plug passed. Not verified: Spotify and scrolling during a copy; unplug without eject with a non-empty file.
**Deliverable:** with playback showing on the OLED, the Pico mounts as a USB drive and files can be copied to it and kept.
**Verify:**
- Copy a 100 KB file on and off the drive during a track change, while pressing play/pause. Scrolling continues, Spotify calls still succeed, and the device doesn't crash.
- Eject and re-plug: the file is still there.
- Pull the cable without ejecting: record whether the file survives.
- `pio run -t upload` still works, and serial output still appears.
**Work:**
- `platformio.ini`: add `board_build.filesystem_size` (start at 256k; confirm Windows mounts and formats it).
- `usbConfig.h/cpp`: FatFS and FatFSUSB startup, with plug and unplug callbacks. The ready callback moves to Phase 2, because the firmware doesn't touch the filesystem after startup until then.
- `main.cpp`: call it from `setup()`.
- If WiFi can't tolerate the flash stalls, add a pause to `tickCore1()` while the drive is mounted.
**Not yet:** parsing, EEPROM, or any change to where credentials come from.

### Phase 2 — Change WiFi from `config.txt`
**Status:** implemented. Wrong then right password import passed, and `config.txt` was removed. A flash dump showed test sentinels and the WiFi password absent from the drive region. Stored credentials survived a reflash.
**Deliverable:** dropping `WIFI_SSID` and `WIFI_PASS` and ejecting makes the device reboot onto the new network. `secrets.h` remains the fallback when nothing is stored.
**Verify:**
- Set a wrong password: OLED shows `WiFi failed`. Set the right one: it connects.
- After the reboot, `config.txt` is gone from the drive.
- Reflash the firmware: the stored WiFi values persist.
- Use a sentinel password, then dump flash in BOOTSEL with `picotool save -a dump.bin`. The sentinel appears only in the EEPROM sector, not the FS region.
**Work:**
- `credentials.h/cpp`: load, merge, CRC check, commit.
- `usbConfig.cpp`: parser, unplug flag handling in `loop()`, boot-time check, raw FS erase, reboot.
- `wifiManager.cpp`: read from `credentials`.
**Not yet:** Spotify keys, `status.txt`, and any validation beyond a non-empty SSID.

### Phase 3 — Validation and `status.txt`
**Status:** implemented. Five rejected-file cases passed, with no sentinel in `status.txt`. Not verified: corrupt EEPROM CRC, rejection message on the OLED.
**Deliverable:** a bad file changes nothing and explains why. The drive shows the current state without secrets.
**Verify:**
- Try a 7-character password, an unknown key, a line with no `=`, and an oversized file. Stored values stay unchanged, the OLED shows `Config rejected`, and `status.txt` names the problem.
- Search `status.txt` for the sentinel: no match.
- Corrupt the EEPROM CRC with a test build: the device ignores the record and `status.txt` reports firmware defaults. The true unconfigured state arrives in Phase 5.
- Reboot several times with no changes: `status.txt` content is unchanged. The write is skipped when content matches, by code review, because the device has no reliable clock for modified times.
**Work:**
- Field rules: SSID 1–32 bytes; password empty (open network) or 8–63 bytes; size limits on the Spotify fields; a maximum file size.
- `status.txt`: SSID, set/unset per key, last import result. Write it only when the content changes, to limit flash wear.
**Not yet:** Spotify keys are accepted by the parser only once Phase 4 uses them.

### Phase 4 — Spotify credentials from `config.txt`
**Status:** implemented. Serial confirmed the token is acquired at boot from stored values, a wrong secret returns 400 with `auth failed` and backoff, and restoring it recovers. Not verified: a newly generated refresh token, the OLED error, and playback after import.
**Deliverable:** client ID, secret and refresh token can be changed without reflashing, and the device fetches its own access token at boot.
**Verify:**
- Generate a new refresh token with `get_spotify_token.py`, drop it in: playback appears after reboot, without an initial access token.
- Set a wrong client secret: OLED shows a Spotify auth error, requests are not retried in a tight loop, and fixing the file recovers it.
**Work:**
- `spotify.cpp`: build the Basic header and refresh body at runtime. Start with an empty `_accessToken` and refresh in `tickCore1()` before the first request, since the existing retry only handles 401 and an empty bearer token may not return one. Retry quietly at 5 s when the request never reaches Spotify, because the first request after boot returned `-1` in most tests. Show the error and back off from 5 s to 60 s when Spotify rejects it.
- Extend the parser and `status.txt` with the three keys.
**Not yet:** `secrets.h` remains the fallback.

### Phase 5 — Remove credentials from the firmware
**Deliverable:** the firmware contains no credentials, and a blank device explains how to set itself up.
**Verify:**
- Search `firmware.bin` for the current SSID, client secret and refresh token: no matches.
- Clear flash with `flash_nuke.uf2`, then flash the firmware: OLED shows setup instructions, the drive mounts, and there are no WiFi attempts.
- A full `config.txt` then gets the device playing.
**Work:**
- Delete `secrets.h` and `secrets.h.example`, and their includes and fallbacks.
- `main.cpp`, `wifiManager.cpp`, `spotify.cpp`: add the unconfigured state. WiFi and Spotify stay idle while the drive and display keep working.
- Update `README.md` and `CLAUDE.md`, including the stale `wifi_manager` filename.
- Rotate the WiFi password, client secret and refresh token, which were previously compiled in and printed in a Claude Code session.
**Not yet:** factory reset, PKCE and captive portal.

## Open questions
- **Does an eject trigger the unplug callback reliably on Windows?** If not, Phase 1 must find another signal, such as a button gesture to apply the config. Phase 1 settles this.
- **Is the drive reappearing after reboot acceptable?** After a successful import, the reboot re-mounts the drive, so `status.txt` is visible but the user must eject or unplug again. This plan accepts it.
- **Will a charger-only setup be enough?** A power-only USB source shows no drive, so changing credentials always needs a computer. This plan accepts it.

## Rejected alternatives
- **LittleFS for internal storage:** shares the FS region with FatFS, so it would need a custom partition layout. The EEPROM sector already fits a small credentials struct.
- **Persistent editable config:** readable by anyone who plugs the device in.
- **Encrypting stored credentials:** the RP2040 has no secure key storage, so the key would sit in the same flash.
- **Applying credentials without a reboot:** needs synchronising with Core 1 and resetting WiFi, the token and FTL state, and a reboot gets all of that for free.
- **Deleting `config.txt` or using `FatFS.format()`:** both can leave the secret in flash, so the region is raw-erased instead.
- **Captive portal and BLE:** more firmware than a USB-only workflow needs.
