#ifndef KY_040_H
#define KY_040_H

#include "pico/stdlib.h"

typedef enum
{
    ENCODER_DIRECTION_CLOCKWISE = 1,
    ENCODER_DIRECTION_STOPPED = 0,
    ENCODER_DIRECTION_COUNTERCLOCKWISE = -1
} tEncoderDirection;

void Encoder_Init(void);
void Encoder_Task(void);
bool Encoder_GetReadingOnNextInterrupt(repeating_timer_t *rt);
void SetEncoderPulsedCallback(void (*encoderPulsedCallback)(tEncoderDirection));
void SetEncoderSwitchPressedCallback(void (*encoderSwitchPressedCallback)(void));

#endif
