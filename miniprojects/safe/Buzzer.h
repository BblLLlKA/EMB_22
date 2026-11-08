#pragma once

#include <stdint.h>

/** Plays nonblocking tone sequences on a passive buzzer. */
class Buzzer
{
public:
    explicit Buzzer(uint8_t channel = 0);
    ~Buzzer();

    Buzzer(const Buzzer&) = delete;
    Buzzer& operator=(const Buzzer&) = delete;

    bool begin();
    void beep(uint16_t frequency, uint16_t duration);
    void playError();
    void playSuccess();
    void stop();
    void update();
    bool isPlaying() const;

private:
    struct SoundStep
    {
        uint16_t frequency;
        uint16_t duration;
    };

    enum class State : uint8_t
    {
        Idle,
        PlayingTone,
        Pause
    };

    void startPattern(const SoundStep* steps, uint8_t count);
    void startStep();

    const uint8_t channel;
    bool initialized;
    State state;
    const SoundStep* pattern;
    uint8_t stepCount;
    uint8_t stepIndex;
    uint32_t stepStartedAt;
    SoundStep singleStep;
};
