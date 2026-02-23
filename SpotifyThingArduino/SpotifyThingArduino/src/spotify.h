#pragma once
#include <Arduino.h>

class SpotifyClient {
public:
  SpotifyClient();
  void fetchNowPlaying();

private:
  String _accessToken;
  bool refreshAccessToken();
  static String base64Encode(const String& input);
};
