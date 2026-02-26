#include "spotify.h"
#include "display.h"
#include "secrets.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static constexpr const char* CURRENTLY_PLAYING_URL = "https://api.spotify.com/v1/me/player";
static constexpr const char* TOKEN_URL              = "https://accounts.spotify.com/api/token";
static constexpr const char* PAUSE_URL              = "https://api.spotify.com/v1/me/player/pause";
static constexpr const char* PLAY_URL               = "https://api.spotify.com/v1/me/player/play";
static constexpr const char* NEXT_URL               = "https://api.spotify.com/v1/me/player/next";
static constexpr const char* VOLUME_URL             = "https://api.spotify.com/v1/me/player/volume?volume_percent=";

static constexpr int HTTP_OK              = 200;
static constexpr int HTTP_NO_CONTENT      = 204;
static constexpr int HTTP_UNAUTHORIZED    = 401;
static constexpr int HTTP_TOO_MANY_REQS   = 429;
static constexpr int RATE_LIMIT_DEFAULT_S = 30;
static constexpr int VOLUME_CHANGE_STEP   = 10;

static constexpr unsigned long IDLE_CLOCK_TIMEOUT_MS      = 10UL * 60UL * 1000UL;
static constexpr int           SPOTIFY_SKIP_PROPAGATION_MS = 600;
static constexpr unsigned long VOLUME_DEBOUNCE_MS          = 300;

static constexpr const char* b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


SpotifyClient::SpotifyClient() : _accessToken(SPOTIFY_ACCESS_TOKEN) {
    mutex_init(&_mutex);
}

String SpotifyClient::base64Encode(const String& input) {
    String result;
    int i = 0;
    uint8_t buf3[3], buf4[4];
    const char* bytes = input.c_str();
    int len = input.length();

    while (len--) {
        buf3[i++] = *bytes++;
        if (i == 3) {
            buf4[0] = (buf3[0] & 0xfc) >> 2;
            buf4[1] = ((buf3[0] & 0x03) << 4) + ((buf3[1] & 0xf0) >> 4);
            buf4[2] = ((buf3[1] & 0x0f) << 2) + ((buf3[2] & 0xc0) >> 6);
            buf4[3] =  buf3[2] & 0x3f;
            for (i = 0; i < 4; i++) result += b64chars[buf4[i]];
            i = 0;
        }
    }
    if (i) {
        for (int j = i; j < 3; j++) buf3[j] = 0;
        buf4[0] = (buf3[0] & 0xfc) >> 2;
        buf4[1] = ((buf3[0] & 0x03) << 4) + ((buf3[1] & 0xf0) >> 4);
        buf4[2] = ((buf3[1] & 0x0f) << 2) + ((buf3[2] & 0xc0) >> 6);
        for (int j = 0; j < i + 1; j++) result += b64chars[buf4[j]];
        while (i++ < 3) result += '=';
    }
    return result;
}

bool SpotifyClient::refreshAccessToken() {
    WiFiClientSecure wifiClient;
    wifiClient.setInsecure();

    HTTPClient https;
    https.begin(wifiClient, TOKEN_URL);
    https.addHeader("Content-Type", "application/x-www-form-urlencoded");
    https.addHeader("Authorization", "Basic " + base64Encode(String(SPOTIFY_CLIENT_ID) + ":" + SPOTIFY_CLIENT_SECRET));

    const int httpCode = https.POST("grant_type=refresh_token&refresh_token=" SPOTIFY_REFRESH_TOKEN);

    if (httpCode != HTTP_OK) {
        https.end();
        return false;
    }

    JsonDocument response_json;
    const DeserializationError error = deserializeJson(response_json, https.getStream());
    https.end();

    if (error) return false;

    _accessToken = response_json["access_token"].as<String>();
    return true;
}

void SpotifyClient::requestFetch() {
    _pendingCommand = SpotifyCommand::Fetch;
    _commandPending = true;
}

void SpotifyClient::togglePlayPause() {
    _pendingCommand = SpotifyCommand::TogglePlayPause;
    _commandPending = true;
}

void SpotifyClient::skipTrack() {
    _pendingCommand = SpotifyCommand::Skip;
    _commandPending = true;
}

void SpotifyClient::increaseVolume() {
    _targetVolume  = min((int8_t)100, (int8_t)(_targetVolume + VOLUME_CHANGE_STEP));
    _volumeChangeAt = millis();
}

void SpotifyClient::decreaseVolume() {
    _targetVolume  = max((int8_t)0, (int8_t)(_targetVolume - VOLUME_CHANGE_STEP));
    _volumeChangeAt = millis();
}

void SpotifyClient::tickCore1() {
    if (millis() < _rateLimitUntilMs) return;

    if (_targetVolume != _volume && _volumeChangeAt > 0 &&
        millis() - _volumeChangeAt >= VOLUME_DEBOUNCE_MS) {
        _doSetVolume();
        return;
    }

    if (!_commandPending) return;
    SpotifyCommand command = _pendingCommand;
    _commandPending = false;
    switch (command) {
        case SpotifyCommand::Fetch:           _doFetch();  break;
        case SpotifyCommand::TogglePlayPause: _doToggle(); break;
        case SpotifyCommand::Skip:            _doSkip();   break;
    }
}

void SpotifyClient::_doFetch() {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(5000);

    HTTPClient https;
    https.begin(client, CURRENTLY_PLAYING_URL);
    https.addHeader("Authorization", "Bearer " + _accessToken);
    const char* fetchHdrs[] = {"Retry-After"};
    https.collectHeaders(fetchHdrs, 1);

    const int httpCode = https.GET();

    if (httpCode == HTTP_UNAUTHORIZED) {
        https.end();
        if (refreshAccessToken()) {
            _doFetch();
        } else {
            SpotifyResult result{};
            result.type = SpotifyResult::Type::Error;
            strncpy(result.message, "Auth failed", sizeof(result.message) - 1);
            mutex_enter_blocking(&_mutex);
            _pendingResult = result;
            _resultReady = true;
            mutex_exit(&_mutex);
        }
        return;
    }

    if (httpCode == HTTP_TOO_MANY_REQS) {
        const int wait = https.header("Retry-After").toInt();
        Serial.println("[spotify] 429 rate limited, Retry-After: " + String(wait) + "s");
        https.end();
        SpotifyResult rl{};
        rl.type = SpotifyResult::Type::Error;
        strncpy(rl.message, "Rate limited", sizeof(rl.message) - 1);
        mutex_enter_blocking(&_mutex);
        _pendingResult = rl;
        _resultReady = true;
        mutex_exit(&_mutex);
        _rateLimitUntilMs = millis() + (unsigned long)(wait > 0 ? wait : RATE_LIMIT_DEFAULT_S) * 1000UL;
        return;
    }

    if (httpCode == HTTP_OK) {
        JsonDocument filter;
        filter["is_playing"]                 = true;
        filter["progress_ms"]                = true;
        filter["device"]["volume_percent"]    = true;
        filter["device"]["supports_volume"]   = true;
        filter["item"]["id"]                 = true;
        filter["item"]["name"]               = true;
        filter["item"]["duration_ms"]        = true;
        filter["item"]["artists"][0]["name"] = true;

        JsonDocument jsonResponse;
        const DeserializationError error = deserializeJson(jsonResponse, https.getStream(), DeserializationOption::Filter(filter));
        https.end();

        if (error) {
            SpotifyResult r{};
            r.type = SpotifyResult::Type::Error;
            strncpy(r.message, "Parse error", sizeof(r.message) - 1);
            mutex_enter_blocking(&_mutex);
            _pendingResult = r;
            _resultReady = true;
            mutex_exit(&_mutex);
            return;
        }

        SpotifyResult result{};
        result.type       = SpotifyResult::Type::NowPlaying;
        result.isPlaying  = jsonResponse["is_playing"].as<bool>();
        result.progressMs = jsonResponse["progress_ms"].as<uint32_t>();
        result.durationMs = jsonResponse["item"]["duration_ms"].as<uint32_t>();

        const char* trackId = jsonResponse["item"]["id"];
        const char* artist  = jsonResponse["item"]["artists"][0]["name"];
        const char* track   = jsonResponse["item"]["name"];
        const bool  supportsVolume = jsonResponse["device"]["supports_volume"].as<bool>();
        const int8_t volume  = jsonResponse["device"]["volume_percent"].as<int8_t>();

        if (trackId) strncpy(result.trackId, trackId, sizeof(result.trackId) - 1);
        if (artist)  strncpy(result.artist,  artist,  sizeof(result.artist)  - 1);
        if (track)   strncpy(result.track,   track,   sizeof(result.track)   - 1);

        _isPlaying      = result.isPlaying;
        _volume         = volume;
        _targetVolume   = volume;
        _supportsVolume = supportsVolume;

        mutex_enter_blocking(&_mutex);
        _pendingResult = result;
        _resultReady = true;
        mutex_exit(&_mutex);

    } else if (httpCode == HTTP_NO_CONTENT) {
        https.end();
        SpotifyResult r{};
        r.type = SpotifyResult::Type::Idle;
        mutex_enter_blocking(&_mutex);
        _pendingResult = r;
        _resultReady = true;
        mutex_exit(&_mutex);

    } else {
        https.end();
        SpotifyResult r{};
        r.type = SpotifyResult::Type::Error;
        snprintf(r.message, sizeof(r.message), "%d", httpCode);
        mutex_enter_blocking(&_mutex);
        _pendingResult = r;
        _resultReady = true;
        mutex_exit(&_mutex);
    }
}

void SpotifyClient::_doToggle() {
    WiFiClientSecure wifiClient;
    wifiClient.setInsecure();
    wifiClient.setTimeout(5000);

    int retryAfterSec = RATE_LIMIT_DEFAULT_S;
    auto doRequest = [&]() {
        HTTPClient https;
        https.begin(wifiClient, _isPlaying ? PAUSE_URL : PLAY_URL);
        https.addHeader("Authorization", "Bearer " + _accessToken);
        const char* toggleHdrs[] = {"Retry-After"};
        https.collectHeaders(toggleHdrs, 1);
        const int code = https.PUT("");
        if (code == HTTP_TOO_MANY_REQS) {
            const int w = https.header("Retry-After").toInt();
            if (w > 0) retryAfterSec = w;
        }
        https.end();
        return code;
    };

    int httpCode = doRequest();

    if (httpCode == HTTP_UNAUTHORIZED) {
        if (!refreshAccessToken()) return;
        httpCode = doRequest();
    }

    if (httpCode == HTTP_TOO_MANY_REQS) {
        SpotifyResult rl{};
        rl.type = SpotifyResult::Type::Error;
        strncpy(rl.message, "Rate limited", sizeof(rl.message) - 1);
        mutex_enter_blocking(&_mutex);
        _pendingResult = rl;
        _resultReady = true;
        mutex_exit(&_mutex);
        _rateLimitUntilMs = millis() + (unsigned long)retryAfterSec * 1000UL;
        return;
    }

    if (httpCode == HTTP_NO_CONTENT || httpCode == HTTP_OK) {
        _isPlaying = !_isPlaying;
        SpotifyResult r{};
        r.type      = SpotifyResult::Type::PlayingStateChanged;
        r.isPlaying = _isPlaying;
        mutex_enter_blocking(&_mutex);
        _pendingResult = r;
        _resultReady = true;
        mutex_exit(&_mutex);
    }
}

void SpotifyClient::_doSkip() {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(5000);

    HTTPClient https;
    https.begin(client, NEXT_URL);
    https.addHeader("Authorization", "Bearer " + _accessToken);
    const char* skipHdrs[] = {"Retry-After"};
    https.collectHeaders(skipHdrs, 1);
    const int httpCode = https.POST("");

    if (httpCode == HTTP_UNAUTHORIZED) {
        https.end();
        if (refreshAccessToken()) _doSkip();
        return;
    }

    if (httpCode == HTTP_TOO_MANY_REQS) {
        const int wait = https.header("Retry-After").toInt();
        https.end();
        SpotifyResult rl{};
        rl.type = SpotifyResult::Type::Error;
        strncpy(rl.message, "Rate limited", sizeof(rl.message) - 1);
        mutex_enter_blocking(&_mutex);
        _pendingResult = rl;
        _resultReady = true;
        mutex_exit(&_mutex);
        _rateLimitUntilMs = millis() + (unsigned long)(wait > 0 ? wait : RATE_LIMIT_DEFAULT_S) * 1000UL;
        return;
    }

    https.end();
    sleep_ms(SPOTIFY_SKIP_PROPAGATION_MS);
    _doFetch();
}

void SpotifyClient::_doSetVolume() {
    if (!_supportsVolume) return;

    const int8_t target = _targetVolume;

    WiFiClientSecure wifiClient;
    wifiClient.setInsecure();
    wifiClient.setTimeout(5000);

    int retryAfterSec = RATE_LIMIT_DEFAULT_S;
    auto doRequest = [&]() {
        HTTPClient https;
        https.begin(wifiClient, VOLUME_URL + String(target));
        https.addHeader("Authorization", "Bearer " + _accessToken);
        const char* hdrs[] = {"Retry-After"};
        https.collectHeaders(hdrs, 1);
        const int code = https.PUT("");
        if (code == HTTP_TOO_MANY_REQS) {
            const int w = https.header("Retry-After").toInt();
            if (w > 0) retryAfterSec = w;
        }
        https.end();
        return code;
    };

    int httpCode = doRequest();

    if (httpCode == HTTP_UNAUTHORIZED) {
        if (!refreshAccessToken()) return;
        httpCode = doRequest();
    }

    if (httpCode == HTTP_TOO_MANY_REQS) {
        SpotifyResult result{};
        result.type = SpotifyResult::Type::Error;
        strncpy(result.message, "Rate limited", sizeof(result.message) - 1);
        mutex_enter_blocking(&_mutex);
        _pendingResult = result;
        _resultReady = true;
        mutex_exit(&_mutex);
        _rateLimitUntilMs = millis() + (unsigned long)retryAfterSec * 1000UL;
        return;
    }

    if (httpCode == HTTP_NO_CONTENT || httpCode == HTTP_OK) {
        _volume = target;
        SpotifyResult r{};
        r.type   = SpotifyResult::Type::VolumeChanged;
        r.volume = _volume;
        mutex_enter_blocking(&_mutex);
        _pendingResult = r;
        _resultReady = true;
        mutex_exit(&_mutex);
    }
}

void SpotifyClient::applyPendingResult() {
    if (!_resultReady) return;

    mutex_enter_blocking(&_mutex);
    SpotifyResult r = _pendingResult;
    _resultReady = false;
    mutex_exit(&_mutex);

    switch (r.type) {
        case SpotifyResult::Type::NowPlaying:
            _idleSince = 0;
            displaySetPlaying(r.isPlaying);
            displaySetProgress(r.progressMs, r.durationMs);
            if (_lastTrackId != r.trackId) {
                _lastTrackId = r.trackId;
                displayMessage(r.artist[0] ? r.artist : "Unknown artist",
                               r.track[0]  ? r.track  : "Unknown track");
            }
            break;

        case SpotifyResult::Type::PlayingStateChanged:
            displaySetPlaying(r.isPlaying);
            break;

        case SpotifyResult::Type::VolumeChanged:
            Serial.println("Volume changed to " + String(r.volume));
            break;

        case SpotifyResult::Type::Idle:
            _lastTrackId = "";
            if (_idleSince == 0) _idleSince = millis();
            if (millis() - _idleSince >= IDLE_CLOCK_TIMEOUT_MS) displayEnterClockMode();
            break;

        case SpotifyResult::Type::Error:
            displayMessage("Spotify error", r.message);
            break;
    }
}
