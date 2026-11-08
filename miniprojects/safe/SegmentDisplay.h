#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <initializer_list>

/** Multiplexes seven-segment digits using an ESP32 hardware timer. */
class SegmentDisplay
{
public:
    explicit SegmentDisplay(uint8_t digitCount);
    ~SegmentDisplay();

    SegmentDisplay(const SegmentDisplay&) = delete;
    SegmentDisplay& operator=(const SegmentDisplay&) = delete;

    bool begin();
    uint8_t getDigitCount() const { return digitCount; }
    void displayNumber(uint32_t number);
    void displayDigit(uint8_t position, int8_t value);
    void displayDash();
    void displayDash(std::initializer_list<uint8_t> positions);
    void clear();
    void start();
    void stop();

private:
    static constexpr uint8_t maxDigits = 10;
    static constexpr uint8_t dash = 10;
    static constexpr uint8_t blank = 11;

    struct PinMask
    {
        uint32_t low;
        uint32_t high;
    };

    struct SegmentLevels
    {
        PinMask set;
        PinMask clear;
    };

    struct IsrState
    {
        uint8_t* buffer;
        uint8_t count;
        uint8_t nextDigit;
        bool running;
        PinMask digitMasks[maxDigits];
        SegmentLevels segments[12];
    };

    static portMUX_TYPE mux;
    static SegmentDisplay* owner;
    static IsrState state;

    static void IRAM_ATTR onTimer();
    static void IRAM_ATTR setPins(const PinMask& mask);
    static void IRAM_ATTR clearPins(const PinMask& mask);
    static void IRAM_ATTR disableDigit(uint8_t position);
    static void IRAM_ATTR disableAll();
    static void addPin(PinMask& mask, uint8_t pin);

    const uint8_t digitCount;
    hw_timer_t* timer;
};