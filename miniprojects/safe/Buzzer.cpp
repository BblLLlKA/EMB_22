#include "Buzzer.h"
#include "Config.h"
#include <Arduino.h>
#include <driver/ledc.h>

static_assert(BUZZER_ERROR_FIRST_MS > 0 && BUZZER_ERROR_PAUSE_MS > 0 &&
              BUZZER_ERROR_SECOND_MS > 0 && BUZZER_SUCCESS_FIRST_MS > 0 &&
              BUZZER_SUCCESS_PAUSE_MS > 0 && BUZZER_SUCCESS_SECOND_MS > 0,
              "Buzzer pattern durations must be nonzero");
static_assert(BUZZER_ERROR_FREQUENCY_HZ > 0 && BUZZER_SUCCESS_FIRST_HZ > 0 &&
              BUZZER_SUCCESS_SECOND_HZ > 0, "Buzzer tone frequencies must be nonzero");
static_assert(BUZZER_PWM_RESOLUTION_BITS == 10,
              "Arduino Core 2.x ledcWriteTone requires 10-bit PWM resolution");

Buzzer::Buzzer(uint8_t pwmChannel)
    : channel(pwmChannel), initialized(false), state(State::Idle),
      pattern(nullptr), stepCount(0), stepIndex(0), stepStartedAt(0), singleStep{}
{
}

Buzzer::~Buzzer()
{
    if (initialized)
    {
        stop();
        ledcDetachPin(BUZZER_PIN);
    }
}

bool Buzzer::begin()
{
    if (initialized)
        return true;

    if (channel >= 8 || !digitalPinCanOutput(BUZZER_PIN))
    {
        log_e("Invalid buzzer channel or output GPIO");
        return false;
    }
    if (ledcSetup(channel, BUZZER_INITIAL_FREQUENCY_HZ, BUZZER_PWM_RESOLUTION_BITS) == 0)
    {
        log_e("Cannot configure buzzer PWM");
        return false;
    }
    // Configure the channel with zero duty before routing it to the pin.
    ledc_channel_config_t settings = {};
    settings.gpio_num = BUZZER_PIN;
    settings.speed_mode = LEDC_LOW_SPEED_MODE;
    settings.channel = static_cast<ledc_channel_t>(channel);
    settings.timer_sel = static_cast<ledc_timer_t>(channel / 2);
    settings.intr_type = LEDC_INTR_DISABLE;
    settings.duty = 0;
    if (ledc_channel_config(&settings) != ESP_OK)
    {
        log_e("Cannot attach buzzer PWM");
        return false;
    }
    initialized = true;
    return true;
}

void Buzzer::beep(uint16_t frequency, uint16_t duration)
{
    if (!initialized || isPlaying() || duration == 0)
    {
        log_e("Buzzer unavailable or invalid duration");
        return;
    }
    singleStep = {frequency, duration};
    startPattern(&singleStep, 1);
}

void Buzzer::playError()
{
    static const SoundStep steps[] = {
        {BUZZER_ERROR_FREQUENCY_HZ, BUZZER_ERROR_FIRST_MS},
        {0, BUZZER_ERROR_PAUSE_MS},
        {BUZZER_ERROR_FREQUENCY_HZ, BUZZER_ERROR_SECOND_MS}
    };
    startPattern(steps, sizeof(steps) / sizeof(steps[0]));
}

void Buzzer::playSuccess()
{
    static const SoundStep steps[] = {
        {BUZZER_SUCCESS_FIRST_HZ, BUZZER_SUCCESS_FIRST_MS},
        {0, BUZZER_SUCCESS_PAUSE_MS},
        {BUZZER_SUCCESS_SECOND_HZ, BUZZER_SUCCESS_SECOND_MS}
    };
    startPattern(steps, sizeof(steps) / sizeof(steps[0]));
}

void Buzzer::startPattern(const SoundStep* steps, uint8_t count)
{
    if (!initialized || isPlaying())
    {
        log_e("Buzzer is not initialized or is already playing");
        return;
    }
    pattern = steps;
    stepCount = count;
    stepIndex = 0;
    startStep();
}

void Buzzer::startStep()
{
    const SoundStep& step = pattern[stepIndex];
    if (step.frequency == 0)
    {
        ledcWriteTone(channel, 0);
        state = State::Pause;
    }
    else
    {
        if (ledcWriteTone(channel, step.frequency) == 0)
        {
            log_e("Cannot generate buzzer frequency: %u", static_cast<unsigned>(step.frequency));
            stop();
            return;
        }
        state = State::PlayingTone;
    }
    stepStartedAt = millis();
}

void Buzzer::stop()
{
    if (initialized)
        ledcWriteTone(channel, 0);
    state = State::Idle;
    pattern = nullptr;
    stepCount = 0;
    stepIndex = 0;
}

void Buzzer::update()
{
    if (!isPlaying())
        return;

    const uint32_t now = millis();
    if (now - stepStartedAt < pattern[stepIndex].duration)
        return;

    if (++stepIndex == stepCount)
        stop();
    else
        startStep();
}

bool Buzzer::isPlaying() const
{
    return state != State::Idle;
}
