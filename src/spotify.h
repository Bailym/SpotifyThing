#pragma once
#include <Arduino.h>

class SpotifyClient {
public:
  SpotifyClient();
  void fetchNowPlaying(bool retry = true);
  void togglePlayPause();
  void skipTrack();

private:
  String        _accessToken;
  String        _lastTrackId;
  unsigned long _idleSince = 0;
  bool          _isPlaying = false;
  bool refreshAccessToken();
  static String base64Encode(const String& input);
};
