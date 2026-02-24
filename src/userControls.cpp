#include "userControls.h"
#include "spotify.h"
#include "Arduino.h"

extern SpotifyClient spotify;

extern "C"
{
#include "ky-040.h"
}

static void onClockwise()
{
    Serial.println("Clockwise");
}

static void onCounterclockwise()
{
    Serial.println("Counterclockwise");
}

static void onSwitchPressed()
{
    spotify.togglePlayPause();
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
    Serial.println("Initializing default controls");
    Encoder_Init();
    SetEncoderPulsedCallback(onEncoderPulsed);
    SetEncoderSwitchPressedCallback(onSwitchPressed);
}

void userControlsTick()
{
    Encoder_Task();
}

void clearDefaultControls()
{
    SetEncoderPulsedCallback(nullptr);
    SetEncoderSwitchPressedCallback(nullptr);
}
