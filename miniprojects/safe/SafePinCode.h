#pragma once

#include <stdint.h>
#include <RotaryEncoder.h>

class SegmentDisplay;

/** Handles nonblocking, fixed-width PIN entry through an encoder and display. */
class SafePinCode
{
public:
    SafePinCode(SegmentDisplay& display, uint8_t clk, uint8_t dt, uint8_t sw,
                RotaryEncoder::LatchMode latchMode = RotaryEncoder::LatchMode::FOUR3);
    ~SafePinCode();

    SafePinCode(const SafePinCode&) = delete;
    SafePinCode& operator=(const SafePinCode&) = delete;

    bool begin();
    void update();
    bool isPinCorrect() const;
    bool wasPinEntered();
    bool hasPin() const;
    bool wasPinSet();
    void setPin(uint32_t newPin);
    uint32_t getPin() const;
    void resetInput();
    bool isInputInProgress() const;

private:
    bool pinFits(uint32_t value) const;
    void resetSelection();
    void showSelection();
    void updateBlink();
    void updateButton();
    void confirmDigit();

    SegmentDisplay& display;
    const uint8_t clkPin;
    const uint8_t dtPin;
    const uint8_t swPin;
    const RotaryEncoder::LatchMode latchMode;
    uint32_t storedPin;
    RotaryEncoder* encoder;
    uint8_t* input;
    uint8_t digitCount;
    uint8_t enteredDigits;
    uint8_t selectedDigit;
    bool initialized;
    bool inputInProgress;
    bool pinCorrect;
    bool pinEntered;
    bool pinConfigured;
    bool pinSet;
    bool rawButton;
    bool stableButton;
    bool buttonArmed;
    uint32_t buttonChangedAt;
    bool digitVisible;
    uint32_t blinkChangedAt;
};
