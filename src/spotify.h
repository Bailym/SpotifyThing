#pragma once
#include <Arduino.h>
#include <pico/mutex.h>

enum class SpotifyCommand : uint8_t { Fetch, TogglePlayPause, Skip };

struct SpotifyResult {
    enum class Type : uint8_t { NowPlaying, PlayingStateChanged, Idle, Error };
    Type     type;
    bool     isPlaying;
    uint32_t progressMs;
    uint32_t durationMs;
    char     trackId[64];
    char     artist[128];
    char     track[128];
    char     message[64];
};

class SpotifyClient {
public:
    SpotifyClient();
    void requestFetch();
    void togglePlayPause();
    void skipTrack();
    void tickCore1();
    void applyPendingResult();

private:
    String        _accessToken;
    String        _lastTrackId;
    unsigned long _idleSince = 0;
    bool          _isPlaying = false;

    volatile SpotifyCommand _pendingCommand;
    volatile bool           _commandPending = false;

    SpotifyResult  _pendingResult;
    volatile bool  _resultReady = false;
    mutex_t        _mutex;

    bool refreshAccessToken();
    void _doFetch(bool retrying = false);
    void _doToggle(bool retrying = false);
    void _doSkip(bool retrying = false);
    static String base64Encode(const String& input);
};
