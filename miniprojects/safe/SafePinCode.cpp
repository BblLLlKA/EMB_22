#include "SafePinCode.h"
#include "Config.h"
#include "SegmentDisplay.h"

#include <Arduino.h>
#include <stdint.h>
#include <new>

static_assert(ACTIVE_DIGIT_BLINK_MS > 0, "Blink interval must be nonzero");

SafePinCode::SafePinCode(SegmentDisplay& screen, uint8_t clk, uint8_t dt, uint8_t sw,
                         RotaryEncoder::LatchMode mode)
    : display(screen), clkPin(clk), dtPin(dt), swPin(sw), latchMode(mode),
      storedPin(0), encoder(nullptr), input(nullptr), digitCount(0),
      enteredDigits(0), selectedDigit(0), initialized(false), inputInProgress(false),
      pinCorrect(false), pinEntered(false), pinConfigured(false), pinSet(false),
      rawButton(HIGH), stableButton(HIGH),
      buttonArmed(false), buttonChangedAt(0), digitVisible(true), blinkChangedAt(0)
{
}

SafePinCode::~SafePinCode()
{
    delete encoder;
    delete[] input;
}

bool SafePinCode::pinFits(uint32_t value) const
{
    for (uint8_t i = 0; i < digitCount; ++i)
        value /= 10;
    return value == 0;
}

bool SafePinCode::begin()
{
    if (initialized)
        return true;

    digitCount = display.getDigitCount();
    if (digitCount == 0 || (pinConfigured && !pinFits(storedPin)))
    {
        log_e("Invalid display width or PIN");
        return false;
    }
    if (!digitalPinIsValid(clkPin) || !digitalPinIsValid(dtPin) ||
        !digitalPinIsValid(swPin) || clkPin == dtPin || clkPin == swPin || dtPin == swPin)
    {
        log_e("Invalid or duplicate encoder GPIO");
        return false;
    }
    input = new (std::nothrow) uint8_t[digitCount];
    if (input == nullptr)
    {
        log_e("Cannot allocate PIN input buffer");
        return false;
    }
    if (!display.begin())
    {
        log_e("Cannot initialize PIN display");
        delete[] input;
        input = nullptr;
        return false;
    }
    encoder = new (std::nothrow) RotaryEncoder(clkPin, dtPin, latchMode);
    if (encoder == nullptr)
    {
        log_e("Cannot allocate encoder");
        delete[] input;
        input = nullptr;
        return false;
    }

    pinMode(swPin, INPUT_PULLUP);
    rawButton = stableButton = digitalRead(swPin);
    buttonArmed = stableButton == HIGH;
    buttonChangedAt = millis();
    initialized = true;
    resetInput();
    return true;
}

void SafePinCode::resetSelection()
{
    selectedDigit = 0;
    encoder->setPosition(0);
    showSelection();
}

void SafePinCode::showSelection()
{
    digitVisible = true;
    blinkChangedAt = millis();
    display.displayDigit(digitCount - 1 - enteredDigits, selectedDigit);
}

void SafePinCode::updateBlink()
{
    const uint32_t now = millis();
    const uint32_t intervals = (now - blinkChangedAt) / ACTIVE_DIGIT_BLINK_MS;
    if (intervals == 0)
        return;

    blinkChangedAt += intervals * ACTIVE_DIGIT_BLINK_MS;
    if (intervals % 2 != 0)
    {
        digitVisible = !digitVisible;
        display.displayDigit(digitCount - 1 - enteredDigits, digitVisible ? selectedDigit : -2);
    }
}

void SafePinCode::resetInput()
{
    if (!initialized)
    {
        log_e("PIN input is not initialized");
        return;
    }
    for (uint8_t i = 0; i < digitCount; ++i)
        input[i] = 0;
    enteredDigits = 0;
    inputInProgress = false;
    display.displayDash();
    resetSelection();
}

void SafePinCode::update()
{
    if (!initialized)
        return;

    encoder->tick();
    const long position = encoder->getPosition();
    if (position != selectedDigit)
    {
        const RotaryEncoder::Direction direction = encoder->getDirection();
        if (direction != RotaryEncoder::Direction::NOROTATION)
        {
            long wrapped = position % 10;
            if (wrapped < 0)
                wrapped += 10;
            selectedDigit = static_cast<uint8_t>(wrapped);
            encoder->setPosition(selectedDigit);
            showSelection();
            inputInProgress = true;
        }
    }
    updateButton();
    updateBlink();
}

void SafePinCode::updateButton()
{
    const bool reading = digitalRead(swPin);
    const uint32_t now = millis();
    if (reading != rawButton)
    {
        rawButton = reading;
        buttonChangedAt = now;
    }
    if (reading == stableButton || now - buttonChangedAt < BUTTON_DEBOUNCE_MS)
        return;

    stableButton = reading;
    if (stableButton == LOW)
    {
        if (buttonArmed)
        {
            buttonArmed = false;
            confirmDigit();
        }
    }
    else
    {
        buttonArmed = true;
    }
}

void SafePinCode::confirmDigit()
{
    display.displayDigit(digitCount - 1 - enteredDigits, selectedDigit);
    input[enteredDigits++] = selectedDigit;
    inputInProgress = true;
    if (enteredDigits < digitCount)
    {
        resetSelection();
        return;
    }

    if (!pinConfigured)
    {
        // Compare fixed-width entries numerically while preserving leading zeroes.
        uint64_t value = 0;
        for (uint8_t i = 0; i < digitCount; ++i)
            value = value * 10 + input[i];
        if (value > UINT32_MAX)
        {
            log_e("PIN exceeds uint32_t range");
            resetInput();
            return;
        }
        storedPin = static_cast<uint32_t>(value);
        pinConfigured = true;
        pinSet = true;
    }
    else
    {
        uint32_t expected = storedPin;
        bool match = true;
        for (uint8_t i = digitCount; i > 0; --i)
        {
            if (input[i - 1] != expected % 10)
                match = false;
            expected /= 10;
        }
        pinCorrect = match;
        pinEntered = true;
    }
    resetInput();
}

bool SafePinCode::isPinCorrect() const
{
    return pinCorrect;
}

bool SafePinCode::wasPinEntered()
{
    const bool result = pinEntered;
    pinEntered = false;
    return result;
}

bool SafePinCode::hasPin() const
{
    return pinConfigured;
}

bool SafePinCode::wasPinSet()
{
    const bool result = pinSet;
    pinSet = false;
    return result;
}

void SafePinCode::setPin(uint32_t newPin)
{
    digitCount = display.getDigitCount();
    if (digitCount == 0 || !pinFits(newPin))
    {
        log_e("PIN does not fit the display");
        return;
    }
    storedPin = newPin;
    pinConfigured = true;
}

uint32_t SafePinCode::getPin() const
{
    if (!pinConfigured)
        log_e("PIN has not been set");
    return storedPin;
}

bool SafePinCode::isInputInProgress() const
{
    return inputInProgress;
}
