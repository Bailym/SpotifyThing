#include "spotify.h"
#include "display.h"
#include "secrets.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static constexpr const char* CURRENTLY_PLAYING_URL = "https://api.spotify.com/v1/me/player/currently-playing";
static constexpr const char* TOKEN_URL              = "https://accounts.spotify.com/api/token";
static constexpr const char* PAUSE_URL              = "https://api.spotify.com/v1/me/player/pause";
static constexpr const char* PLAY_URL               = "https://api.spotify.com/v1/me/player/play";
static constexpr const char* NEXT_URL               = "https://api.spotify.com/v1/me/player/next";

static constexpr int HTTP_OK           = 200;
static constexpr int HTTP_NO_CONTENT   = 204;
static constexpr int HTTP_UNAUTHORIZED = 401;

static constexpr unsigned long IDLE_CLOCK_TIMEOUT_MS = 10UL * 60UL * 1000UL;

static constexpr const char* b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

SpotifyClient::SpotifyClient() : _accessToken(SPOTIFY_ACCESS_TOKEN) {}

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
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.begin(client, TOKEN_URL);
  https.addHeader("Content-Type", "application/x-www-form-urlencoded");
  https.addHeader("Authorization", "Basic " + base64Encode(String(SPOTIFY_CLIENT_ID) + ":" + SPOTIFY_CLIENT_SECRET));

  const int httpCode = https.POST("grant_type=refresh_token&refresh_token=" SPOTIFY_REFRESH_TOKEN);

  if (httpCode != HTTP_OK) {
    https.end();
    return false;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, https.getStream());
  https.end();

  if (error) return false;

  _accessToken = doc["access_token"].as<String>();
  return true;
}

void SpotifyClient::togglePlayPause() {
  fetchNowPlaying();

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(5000);

  auto doRequest = [&]() {
    HTTPClient https;
    https.begin(client, _isPlaying ? PAUSE_URL : PLAY_URL);
    https.addHeader("Authorization", "Bearer " + _accessToken);
    const int code = https.PUT("");
    https.end();
    return code;
  };

  int httpCode = doRequest();

  if (httpCode == HTTP_UNAUTHORIZED) {
    if (!refreshAccessToken()) return;
    httpCode = doRequest();
  }

  if (httpCode == HTTP_NO_CONTENT || httpCode == HTTP_OK) {
    _isPlaying = !_isPlaying;
    displaySetPlaying(_isPlaying);
  }
}

void SpotifyClient::skipTrack() {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(5000);

  HTTPClient https;
  https.begin(client, NEXT_URL);
  https.addHeader("Authorization", "Bearer " + _accessToken);
  const int httpCode = https.POST("");
  https.end();

  if (httpCode == HTTP_UNAUTHORIZED) {
    if (refreshAccessToken()) skipTrack();
    return;
  }

  fetchNowPlaying();
}

void SpotifyClient::fetchNowPlaying(bool retry) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(5000);

  HTTPClient https;
  https.begin(client, CURRENTLY_PLAYING_URL);
  https.addHeader("Authorization", "Bearer " + _accessToken);

  const int httpCode = https.GET();

  if (httpCode == HTTP_UNAUTHORIZED) {
    https.end();
    if (refreshAccessToken()) {
      fetchNowPlaying();
    } else {
      displayMessage("Auth failed");
    }
    return;
  }

  if (httpCode == HTTP_OK) {
    JsonDocument filter;
    filter["is_playing"]          = true;
    filter["progress_ms"]         = true;
    filter["item"]["id"]          = true;
    filter["item"]["name"]        = true;
    filter["item"]["duration_ms"] = true;
    filter["item"]["artists"][0]["name"] = true;

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, https.getStream(), DeserializationOption::Filter(filter));
    https.end();

    if (error) {
      displayMessage("Parse error");
      return;
    }

    _idleSince = 0;
    _isPlaying = doc["is_playing"].as<bool>();
    displaySetPlaying(_isPlaying);
    displaySetProgress(doc["progress_ms"].as<uint32_t>(), doc["item"]["duration_ms"].as<uint32_t>());

    const char* trackId = doc["item"]["id"];
    if (trackId && _lastTrackId == trackId) return;
    _lastTrackId = trackId ? trackId : "";

    const char* artist = doc["item"]["artists"][0]["name"];
    const char* track  = doc["item"]["name"];
    displayMessage(artist ? artist : "Unknown artist", track ? track : "Unknown track");

  } else if (httpCode == HTTP_NO_CONTENT) {
    https.end();
    _lastTrackId = "";
    if (_idleSince == 0) _idleSince = millis();
    if (millis() - _idleSince >= IDLE_CLOCK_TIMEOUT_MS) displayEnterClockMode();
  } else if (httpCode == -1 && retry) {
    https.end();
    fetchNowPlaying(false);
  } else {
    https.end();
    displayMessage("Spotify error", String(httpCode).c_str());
  }
}
