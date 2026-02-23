#pragma once
#include <Arduino.h>

class SpotifyClient {
public:
  SpotifyClient();
  void fetchNowPlaying();

private:
  String        _accessToken;
  String        _lastTrackId;
  unsigned long _idleSince = 0;
  bool refreshAccessToken();
  static String base64Encode(const String& input);
};
