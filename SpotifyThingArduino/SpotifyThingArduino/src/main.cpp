#include <Arduino.h>
#include "display.h"
#include "wifi_manager.h"
#include "spotify.h"

static const unsigned long FETCH_INTERVAL_MS = 3000;

SpotifyClient spotify;

void setup() {
  displayInit();
  wifiConnect();
  displayMessage("Nothing playing...");
  spotify.fetchNowPlaying();
}

void loop() {
  static unsigned long lastFetch = 0;

  displayTick();

  if (millis() - lastFetch >= FETCH_INTERVAL_MS) {
    lastFetch = millis();
    spotify.fetchNowPlaying();
  }

  delay(10);
}
