#include <Arduino.h>
#include "display.h"
#include "wifiManager.h"
#include "spotify.h"
#include "userControls.h"

static const unsigned long FETCH_INTERVAL_MS = 3000;
static const int MAIN_LOOP_DELAY_MS = 10;

SpotifyClient spotifyClient;

void setup()
{
  Serial.begin(115200);
  initDefaultControls();
  displayInit();
  wifiConnect();
  displayMessage("Nothing playing...");
  spotifyClient.fetchNowPlaying();
}

void loop()
{
  static unsigned long lastFetch = 0;

  displayTick();
  userControlsTick();

  if (millis() - lastFetch >= FETCH_INTERVAL_MS)
  {
    lastFetch = millis();
    spotifyClient.fetchNowPlaying();
  }

  delay(MAIN_LOOP_DELAY_MS);
}
