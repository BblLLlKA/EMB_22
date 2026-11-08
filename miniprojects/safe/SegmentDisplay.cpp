#include "SegmentDisplay.h"

#include "Config.h"
#include <esp_heap_caps.h>
#include <soc/gpio_struct.h>

namespace
{
constexpr uint32_t apbClockHz = 80000000;
static_assert(DISPLAY_TIMER_DIVIDER >= 2 && DISPLAY_TIMER_DIVIDER <= 65535 &&
              apbClockHz % DISPLAY_TIMER_DIVIDER == 0,
              "Display timer divider must divide the 80 MHz APB clock");
constexpr uint32_t timerTicksPerSecond = apbClockHz / DISPLAY_TIMER_DIVIDER;
static_assert(DISPLAY_REFRESH_HZ > 0 && timerTicksPerSecond % DISPLAY_REFRESH_HZ == 0,
              "Display refresh rate must divide the timer frequency");
constexpr uint32_t refreshInterval = timerTicksPerSecond / DISPLAY_REFRESH_HZ;
constexpr size_t segmentCount = sizeof(SEGMENT_PINS) / sizeof(SEGMENT_PINS[0]);
constexpr size_t configuredDigits = sizeof(DIGIT_ENABLE_PINS) / sizeof(DIGIT_ENABLE_PINS[0]);

static_assert(segmentCount == 7, "Exactly seven segment pins are required");
static_assert(sizeof(DIGIT_PATTERNS) / sizeof(DIGIT_PATTERNS[0]) == 10,
              "Exactly ten digit patterns are required");
}

DRAM_ATTR portMUX_TYPE SegmentDisplay::mux = portMUX_INITIALIZER_UNLOCKED;
SegmentDisplay* SegmentDisplay::owner = nullptr;
DRAM_ATTR SegmentDisplay::IsrState SegmentDisplay::state = {};

SegmentDisplay::SegmentDisplay(uint8_t count) : digitCount(count), timer(nullptr)
{
}

SegmentDisplay::~SegmentDisplay()
{
    if (timer == nullptr)
        return;

    stop();
    timerDetachInterrupt(timer);
    timerEnd(timer);
    timer = nullptr;

    portENTER_CRITICAL(&mux);
    uint8_t* buffer = state.buffer;
    state.buffer = nullptr;
    owner = nullptr;
    portEXIT_CRITICAL(&mux);
    heap_caps_free(buffer);
}

void SegmentDisplay::addPin(PinMask& mask, uint8_t pin)
{
    if (pin < 32)
        mask.low |= uint32_t(1) << pin;
    else
        mask.high |= uint32_t(1) << (pin - 32);
}

bool SegmentDisplay::begin()
{
    if (timer != nullptr)
        return true;

    if (digitCount == 0 || digitCount > maxDigits || digitCount > configuredDigits)
    {
        log_e("Invalid digit count: %u (configured: %u, maximum: %u)",
              digitCount, static_cast<unsigned>(configuredDigits), maxDigits);
        return false;
    }
    if ((SEGMENT_ON_LEVEL != LOW && SEGMENT_ON_LEVEL != HIGH) ||
        (SEGMENT_OFF_LEVEL != LOW && SEGMENT_OFF_LEVEL != HIGH) ||
        SEGMENT_OFF_LEVEL == SEGMENT_ON_LEVEL ||
        (DIGIT_ENABLE_ON_LEVEL != LOW && DIGIT_ENABLE_ON_LEVEL != HIGH) ||
        (DIGIT_ENABLE_OFF_LEVEL != LOW && DIGIT_ENABLE_OFF_LEVEL != HIGH) ||
        DIGIT_ENABLE_OFF_LEVEL == DIGIT_ENABLE_ON_LEVEL)
    {
        log_e("Invalid display polarity");
        return false;
    }

    PinMask used = {};
    for (size_t i = 0; i < segmentCount + digitCount; ++i)
    {
        const uint8_t pin = i < segmentCount ? SEGMENT_PINS[i] : DIGIT_ENABLE_PINS[i - segmentCount];
        if (!digitalPinCanOutput(pin))
        {
            log_e("Invalid output GPIO: %u", pin);
            return false;
        }
        PinMask pinMask = {};
        addPin(pinMask, pin);
        if ((used.low & pinMask.low) || (used.high & pinMask.high))
        {
            log_e("Duplicate display GPIO: %u", pin);
            return false;
        }
        used.low |= pinMask.low;
        used.high |= pinMask.high;
    }

    portENTER_CRITICAL(&mux);
    const bool available = owner == nullptr;
    if (available)
        owner = this;
    portEXIT_CRITICAL(&mux);
    if (!available)
    {
        log_e("A SegmentDisplay already owns the display timer");
        return false;
    }

    uint8_t* buffer = static_cast<uint8_t*>(
        heap_caps_malloc(digitCount, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (buffer == nullptr)
    {
        log_e("Cannot allocate display buffer");
        portENTER_CRITICAL(&mux);
        owner = nullptr;
        portEXIT_CRITICAL(&mux);
        return false;
    }

    timer = timerBegin(DISPLAY_TIMER_INDEX, DISPLAY_TIMER_DIVIDER, true);
    if (timer == nullptr)
    {
        log_e("Cannot allocate display timer");
        heap_caps_free(buffer);
        portENTER_CRITICAL(&mux);
        owner = nullptr;
        portEXIT_CRITICAL(&mux);
        return false;
    }
    timerStop(timer);

    state = {};
    state.buffer = buffer;
    state.count = digitCount;
    for (uint8_t i = 0; i < digitCount; ++i)
    {
        buffer[i] = blank;
        addPin(state.digitMasks[i], DIGIT_ENABLE_PINS[i]);
        disableDigit(i);
        pinMode(DIGIT_ENABLE_PINS[i], OUTPUT);
    }
    for (size_t i = 0; i < segmentCount; ++i)
    {
        PinMask pin = {};
        addPin(pin, SEGMENT_PINS[i]);
        if (SEGMENT_OFF_LEVEL == HIGH)
            setPins(pin);
        else
            clearPins(pin);
        pinMode(SEGMENT_PINS[i], OUTPUT);
        for (uint8_t glyph = 0; glyph <= blank; ++glyph)
        {
            const uint8_t pattern = glyph < dash ? DIGIT_PATTERNS[glyph] :
                                    glyph == dash ? 0x40 : 0;
            const bool on = (pattern & (uint8_t(1) << i)) != 0;
            const bool high = on ? SEGMENT_ON_LEVEL == HIGH : SEGMENT_OFF_LEVEL == HIGH;
            addPin(high ? state.segments[glyph].set : state.segments[glyph].clear,
                   SEGMENT_PINS[i]);
        }
    }

    timerAttachInterrupt(timer, &SegmentDisplay::onTimer, false);
    timerAlarmWrite(timer, refreshInterval, true);
    timerAlarmEnable(timer);
    start();
    return true;
}

void IRAM_ATTR SegmentDisplay::setPins(const PinMask& mask)
{
    GPIO.out_w1ts = mask.low;
    GPIO.out1_w1ts.val = mask.high;
}

void IRAM_ATTR SegmentDisplay::clearPins(const PinMask& mask)
{
    GPIO.out_w1tc = mask.low;
    GPIO.out1_w1tc.val = mask.high;
}

void IRAM_ATTR SegmentDisplay::disableDigit(uint8_t position)
{
    if (DIGIT_ENABLE_OFF_LEVEL == HIGH)
        setPins(state.digitMasks[position]);
    else
        clearPins(state.digitMasks[position]);
}

void IRAM_ATTR SegmentDisplay::disableAll()
{
    for (uint8_t i = 0; i < state.count; ++i)
        disableDigit(i);
    setPins(state.segments[blank].set);
    clearPins(state.segments[blank].clear);
}

void IRAM_ATTR SegmentDisplay::onTimer()
{
    portENTER_CRITICAL_ISR(&mux);
    if (state.running)
    {
        // Disable the previous digit before changing shared segment lines.
        const uint8_t current = state.nextDigit == 0 ? state.count - 1 : state.nextDigit - 1;
        disableDigit(current);

        const uint8_t position = state.nextDigit;
        const uint8_t glyph = state.buffer[position];
        setPins(state.segments[glyph].set);
        clearPins(state.segments[glyph].clear);
        if (glyph != blank)
        {
            if (DIGIT_ENABLE_ON_LEVEL == HIGH)
                setPins(state.digitMasks[position]);
            else
                clearPins(state.digitMasks[position]);
        }
        state.nextDigit = position + 1 == state.count ? 0 : position + 1;
    }
    portEXIT_CRITICAL_ISR(&mux);
}

void SegmentDisplay::displayNumber(uint32_t number)
{
    if (timer == nullptr)
    {
        log_e("Display is not initialized");
        return;
    }
    uint8_t values[maxDigits];
    for (uint8_t i = 0; i < digitCount; ++i)
    {
        values[i] = number % 10;
        number /= 10;
    }
    if (number != 0)
    {
        for (uint8_t i = 0; i < digitCount; ++i)
            values[i] = dash;
    }

    portENTER_CRITICAL(&mux);
    for (uint8_t i = 0; i < digitCount; ++i)
        state.buffer[i] = values[i];
    portEXIT_CRITICAL(&mux);
}

void SegmentDisplay::displayDigit(uint8_t position, int8_t value)
{
    if (timer == nullptr || position >= digitCount || value < -2 || value > 9)
    {
        log_e("Invalid display state, position or digit");
        return;
    }
    const uint8_t glyph = value == -2 ? blank : value == -1 ? dash : value;
    portENTER_CRITICAL(&mux);
    state.buffer[position] = glyph;
    portEXIT_CRITICAL(&mux);
}

void SegmentDisplay::displayDash()
{
    if (timer == nullptr)
    {
        log_e("Display is not initialized");
        return;
    }
    portENTER_CRITICAL(&mux);
    for (uint8_t i = 0; i < digitCount; ++i)
        state.buffer[i] = dash;
    portEXIT_CRITICAL(&mux);
}

void SegmentDisplay::displayDash(std::initializer_list<uint8_t> positions)
{
    if (timer == nullptr)
    {
        log_e("Display is not initialized");
        return;
    }
    for (uint8_t position : positions)
    {
        if (position >= digitCount)
        {
            log_e("Invalid display position: %u", position);
            return;
        }
    }
    portENTER_CRITICAL(&mux);
    for (uint8_t position : positions)
        state.buffer[position] = dash;
    portEXIT_CRITICAL(&mux);
}

void SegmentDisplay::clear()
{
    if (timer == nullptr)
    {
        log_e("Display is not initialized");
        return;
    }
    portENTER_CRITICAL(&mux);
    for (uint8_t i = 0; i < digitCount; ++i)
        state.buffer[i] = blank;
    disableAll();
    portEXIT_CRITICAL(&mux);
}

void SegmentDisplay::start()
{
    if (timer == nullptr)
    {
        log_e("Display is not initialized");
        return;
    }
    portENTER_CRITICAL(&mux);
    const bool wasRunning = state.running;
    state.running = true;
    portEXIT_CRITICAL(&mux);
    if (!wasRunning)
    {
        timerWrite(timer, 0);
        timerStart(timer);
    }
}

void SegmentDisplay::stop()
{
    if (timer == nullptr)
    {
        log_e("Display is not initialized");
        return;
    }
    timerStop(timer);
    portENTER_CRITICAL(&mux);
    state.running = false;
    disableAll();
    portEXIT_CRITICAL(&mux);
}