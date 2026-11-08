#include <Arduino.h>
#include "Config.h"
#include "SegmentDisplay.h"
#include "SafePinCode.h"
#include "Buzzer.h"
#include "ServoController.h"

SegmentDisplay display(DISPLAY_DIGIT_COUNT);
static_assert(ENCODER_LATCH_MODE >= 1 && ENCODER_LATCH_MODE <= 3,
              "Invalid RotaryEncoder latch mode");
static_assert(BUZZER_LEDC_CHANNEL < 8 && SERVO_LEDC_TIMER < 4 &&
                  BUZZER_LEDC_CHANNEL / 2 != SERVO_LEDC_TIMER,
              "Buzzer and servo must use distinct ESP32-S3 LEDC timers");

SafePinCode safe(display, ENCODER_CLK_PIN, ENCODER_DT_PIN, ENCODER_SW_PIN,
                 static_cast<RotaryEncoder::LatchMode>(ENCODER_LATCH_MODE));
Buzzer buzzer(BUZZER_LEDC_CHANNEL);
ServoController servoController;
bool ready = false;

void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);

    if (!display.begin())
    {
        Serial.println("Display initialization failed");
        return;
    }
    if (!safe.begin())
    {
        Serial.println("PIN input initialization failed");
        display.stop();
        return;
    }
    if (!buzzer.begin())
    {
        Serial.println("Buzzer initialization failed");
        display.stop();
        return;
    }
    if (!servoController.begin())
    {
        Serial.println("Servo initialization failed");
        display.stop();
        return;
    }
    Serial.println("Initialization complete");
    ready = true;
}

void loop()
{
    if (!ready)
        return;

    safe.update();
    buzzer.update();
    servoController.update();
    if (safe.wasPinSet())
    {
        // The first complete entry established the PIN.
        Serial.println("PIN has been set");
        buzzer.playSuccess();
    }
    if (safe.wasPinEntered())
    {
        if (safe.isPinCorrect())
        {
            // Handle a correct PIN.
            Serial.println("Correct PIN entered");
            buzzer.playSuccess();
            servoController.unlock();
        }
        else
        {
            // Handle an incorrect PIN.
            Serial.println("Incorrect PIN entered");
            buzzer.playError();
            servoController.playErrorMovement();
        }
    }
}