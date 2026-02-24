#include "userControls.h"
#include "spotify.h"
#include "Arduino.h"

extern SpotifyClient spotifyClient;

extern "C"
{
#include "ky-040.h"
}

static void onEncoderPulsed(tEncoderDirection direction);
static void onSwitchPressed();
static void onDoubleSwitchPressed();
static void onClockwise();
static void onCounterclockwise();

static const int DOUBLE_PRESS_MS = 500;
static const int DEBOUNCE_MS = 50;
static unsigned long lastPressTime = 0;
static int pendingPresses = 0;


static void onClockwise()
{
}

static void onCounterclockwise()
{
}

static void onSwitchPressed()
{
    const unsigned long now = millis();

    if (now - lastPressTime < DEBOUNCE_MS) return;

    if(pendingPresses > 0 && now - lastPressTime < DOUBLE_PRESS_MS)
    {
        pendingPresses++;
    }
    else
    {
        pendingPresses = 1;
    }

    lastPressTime = now;
}

static void onDoubleSwitchPressed()
{
    spotifyClient.skipTrack();
}

static void onEncoderPulsed(tEncoderDirection direction)
{
    if (direction == ENCODER_DIRECTION_CLOCKWISE)
    {
        onClockwise();
    }
    else if (direction == ENCODER_DIRECTION_COUNTERCLOCKWISE)
    {
        onCounterclockwise();
    }
}

void initDefaultControls()
{
    Encoder_Init();
    SetEncoderPulsedCallback(onEncoderPulsed);
    SetEncoderSwitchPressedCallback(onSwitchPressed);
}

void userControlsTick()
{
    Encoder_Task();

    if (pendingPresses > 0 && millis() - lastPressTime >= DOUBLE_PRESS_MS)
    {
        if (pendingPresses >= 2)
            onDoubleSwitchPressed();
        else
            spotifyClient.togglePlayPause();

        pendingPresses = 0;
    }
}

void clearDefaultControls()
{
    SetEncoderPulsedCallback(nullptr);
    SetEncoderSwitchPressedCallback(nullptr);
}
