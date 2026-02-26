#pragma once
#include <Arduino.h>
#include <pico/mutex.h>

enum class SpotifyCommand : uint8_t { Fetch, TogglePlayPause, Skip };

struct SpotifyResult {
    enum class Type : uint8_t { NowPlaying, PlayingStateChanged, VolumeChanged, Idle, Error };
    Type     type;
    bool     isPlaying;
    uint32_t progressMs;
    uint32_t durationMs;
    char     trackId[64];
    char     artist[128];
    char     track[128];
    char     message[64];
    int8_t   volume;
};

class SpotifyClient {
public:
    SpotifyClient();
    void requestFetch();
    void togglePlayPause();
    void skipTrack();
    void increaseVolume();
    void decreaseVolume();
    void tickCore1();
    void applyPendingResult();

private:
    String        _accessToken;
    String        _lastTrackId;
    unsigned long _idleSince = 0;
    bool          _isPlaying = false;
    unsigned long _rateLimitUntilMs = 0;
    int8_t           _volume         = 0;
    volatile int8_t  _targetVolume   = 0;
    volatile unsigned long _volumeChangeAt = 0;
    bool             _supportsVolume = false;

    volatile SpotifyCommand _pendingCommand;
    volatile bool           _commandPending = false;

    SpotifyResult  _pendingResult;
    volatile bool  _resultReady = false;
    mutex_t        _mutex;

    bool refreshAccessToken();
    int  doPut(const String& url);
    void publishResult(const SpotifyResult& r);
    void handleRateLimit(int waitSec);
    void _doFetch();
    void _doToggle();
    void _doSkip();
    void _doSetVolume();
    static String base64Encode(const String& input);

    int _lastRetryAfter = 0;
};
