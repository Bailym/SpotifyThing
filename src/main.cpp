#include <Arduino.h>
#include <WiFi.h>
#include "display.h"
#include "wifiManager.h"
#include "spotify.h"
#include "userControls.h"
#include "usbConfig.h"
#include "credentials.h"

static const unsigned long FETCH_INTERVAL_MS = 5000;
static const int MAIN_LOOP_DELAY_MS = 10;

SpotifyClient spotifyClient;

// Core 0 ------------------------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    initDefaultControls();
    displayInit();
    credentialsLoad();
    usbConfigInit();

    if (!credentialsHaveWifi())
    {
        displayMessage("Setup needed", "Add config.txt to USB");
        return;
    }

    wifiConnect();
    if (credentialsHaveSpotify())
    {
        displayMessage("Nothing playing...");
    }
    else
    {
        displayMessage("Spotify not set up", "Add config.txt to USB");
    }
    spotifyClient.requestFetch();
}

void loop()
{
    static unsigned long lastFetch = 0;

    if (credentialsHaveWifi() && WiFi.status() != WL_CONNECTED)
    {
        wifiConnect();
        spotifyClient.forceDisplayRefresh();
        lastFetch = millis();
    }

    spotifyClient.applyPendingResult();
    displayTick();
    userControlsTick();
    usbConfigTick();

    if (millis() - lastFetch >= FETCH_INTERVAL_MS)
    {
        lastFetch = millis();
        spotifyClient.requestFetch();
    }

    delay(MAIN_LOOP_DELAY_MS);
}

// Core 1 ------------------------------------------------------------------------------------------

void setup1() { }

void loop1()
{
    spotifyClient.tickCore1();
}
