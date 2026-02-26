#include "ky-040.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#define SWITCH_DEBOUNCE_MS 50

typedef enum
{
    GPIO_LOW = 0,
    GPIO_HIGH = 1
} tGpioPinState;

typedef enum
{
    KY_040_PIN_A  = 20,
    KY_040_PIN_B  = 19,
    KY_040_PIN_SW = 18
} tKy040Pin;

static tGpioPinState lastStateA;
static tGpioPinState lastStateB;
static tGpioPinState lastStateSW;
static tGpioPinState lastSyncedState;
static tGpioPinState nextSyncedState;
static int currentEncoderPosition;
static int encoderPulses;
static tEncoderDirection lastEncoderDirection;
static tEncoderDirection currentEncoderDirection;
static void (*storedEncoderPulsedCallback)(tEncoderDirection);
static void (*storedEncoderSwitchPressedCallback)(void);

static volatile tEncoderDirection pendingCallbackDirection;
static volatile bool pendingCallbackReady = false;
static volatile bool pendingSwitchPress   = false;
static absolute_time_t swIgnoreUntil;

static tGpioPinState GetEncoderPinState(tKy040Pin pin)
{
    return (tGpioPinState)gpio_get(pin);
}

static void UpdateEncoderPositionAndDirection()
{
    if (currentEncoderDirection == ENCODER_DIRECTION_CLOCKWISE)
    {
        currentEncoderPosition++;
        lastEncoderDirection = ENCODER_DIRECTION_CLOCKWISE;
    }
    else if (currentEncoderDirection == ENCODER_DIRECTION_COUNTERCLOCKWISE)
    {
        currentEncoderPosition--;
        lastEncoderDirection = ENCODER_DIRECTION_COUNTERCLOCKWISE;
    }
}

static void UpdateEncoderPulses()
{
    if (lastEncoderDirection == currentEncoderDirection)
    {
        encoderPulses++;
    }
    else
    {
        encoderPulses = 1;
    }

    if (encoderPulses == 2)
    {
        encoderPulses = 0;
        pendingCallbackDirection = lastEncoderDirection;
        pendingCallbackReady     = true;
    }
}

static void UpdateSyncedState()
{
    lastSyncedState = nextSyncedState;
    nextSyncedState = lastSyncedState == GPIO_LOW ? GPIO_HIGH : GPIO_LOW;
}

static void UpdateEncoderCountAndSyncState()
{
    UpdateEncoderPositionAndDirection();
    UpdateEncoderPulses();
    UpdateSyncedState();
}

static void encoderGpioCallback(uint gpio, uint32_t events)
{
    if (gpio == KY_040_PIN_A || gpio == KY_040_PIN_B)
    {
        tGpioPinState currentStateA = GetEncoderPinState(KY_040_PIN_A);
        tGpioPinState currentStateB = GetEncoderPinState(KY_040_PIN_B);

        bool encoderMovingClockwise        = (currentStateA == nextSyncedState && currentStateB == lastSyncedState);
        bool encoderMovingCounterClockwise = (currentStateB == nextSyncedState && currentStateA == lastSyncedState);
        bool encoderNotMoving              = (currentStateA == lastSyncedState  && currentStateB == lastSyncedState);
        bool bothPinsHitNextSyncState      = (currentStateA == nextSyncedState  && currentStateB == nextSyncedState);

        if (encoderMovingClockwise)
        {
            currentEncoderDirection = ENCODER_DIRECTION_CLOCKWISE;
        }
        else if (encoderMovingCounterClockwise)
        {
            currentEncoderDirection = ENCODER_DIRECTION_COUNTERCLOCKWISE;
        }
        else if (encoderNotMoving)
        {
            currentEncoderDirection = ENCODER_DIRECTION_STOPPED;
        }
        else if (bothPinsHitNextSyncState)
        {
            UpdateEncoderCountAndSyncState();
        }

        lastStateA = currentStateA;
        lastStateB = currentStateB;
    }

    if (gpio == KY_040_PIN_SW)
    {
        tGpioPinState currentStateSW = GetEncoderPinState(KY_040_PIN_SW);
        if (currentStateSW == GPIO_LOW && lastStateSW == GPIO_HIGH && time_reached(swIgnoreUntil))
        {
            pendingSwitchPress = true;
        }
        if (currentStateSW != lastStateSW)
        {
            swIgnoreUntil = make_timeout_time_ms(SWITCH_DEBOUNCE_MS);
            lastStateSW = currentStateSW;
        }
    }
}

void Encoder_Init(void)
{
    gpio_init(KY_040_PIN_A);
    gpio_init(KY_040_PIN_B);
    gpio_init(KY_040_PIN_SW);
    gpio_set_dir(KY_040_PIN_A,  GPIO_IN);
    gpio_set_dir(KY_040_PIN_B,  GPIO_IN);
    gpio_set_dir(KY_040_PIN_SW, GPIO_IN);

    lastStateA  = GetEncoderPinState(KY_040_PIN_A);
    lastStateB  = GetEncoderPinState(KY_040_PIN_B);
    lastStateSW = GetEncoderPinState(KY_040_PIN_SW);
    lastSyncedState         = GPIO_LOW;
    nextSyncedState         = GPIO_HIGH;
    currentEncoderPosition  = 0;
    encoderPulses           = 0;
    currentEncoderDirection = ENCODER_DIRECTION_STOPPED;
    lastEncoderDirection    = ENCODER_DIRECTION_STOPPED;
    swIgnoreUntil           = nil_time;

    gpio_set_irq_enabled_with_callback(KY_040_PIN_A,  GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, encoderGpioCallback);
    gpio_set_irq_enabled(KY_040_PIN_B,  GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(KY_040_PIN_SW, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
}

void Encoder_Task(void)
{
    if (pendingCallbackReady)
    {
        pendingCallbackReady = false;
        if (storedEncoderPulsedCallback != NULL)
        {
            storedEncoderPulsedCallback(pendingCallbackDirection);
        }
    }

    if (pendingSwitchPress)
    {
        pendingSwitchPress = false;
        if (storedEncoderSwitchPressedCallback != NULL)
        {
            storedEncoderSwitchPressedCallback();
        }
    }
}

void SetEncoderPulsedCallback(void (*encoderPulsedCallback)(tEncoderDirection))
{
    storedEncoderPulsedCallback = encoderPulsedCallback;
}

void SetEncoderSwitchPressedCallback(void (*encoderSwitchPressedCallback)(void))
{
    storedEncoderSwitchPressedCallback = encoderSwitchPressedCallback;
}
