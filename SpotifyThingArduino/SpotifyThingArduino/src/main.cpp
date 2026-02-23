#include <Arduino.h>
#include "display.h"
#include "wifi_manager.h"
#include "spotify.h"

SpotifyClient spotify;

void setup() {
  displayInit();
  wifiConnect();
  spotify.fetchNowPlaying();
}

void loop() {
  delay(10000);
  spotify.fetchNowPlaying();
}
