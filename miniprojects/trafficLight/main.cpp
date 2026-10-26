#include <Arduino.h>
#include "driver/ledc.h"

constexpr uint8_t kSegmentPins[] = {
    10, // A
    11, // B
    12, // C
    13, // D
    16, // E
    17, // F
    18, // G
};
constexpr uint8_t kDigitEnablePins[] = {
    8,
    9,
};

constexpr uint8_t kSegmentOnLevel = LOW;
constexpr uint8_t kSegmentOffLevel = HIGH;

constexpr uint8_t kDigitEnableOnLevel = HIGH;
constexpr uint8_t kDigitEnableOffLevel = LOW;

constexpr uint8_t kDigitPatterns[] = {
    0b00111111, // 0: A B C D E F
    0b00000110, // 1: B C
    0b01011011, // 2: A B D E G
    0b01001111, // 3: A B C D G
    0b01100110, // 4: B C F G
    0b01101101, // 5: A C D F G
    0b01111101, // 6: A C D E F G
    0b00000111, // 7: A B C
    0b01111111, // 8: A B C D E F G
    0b01101111, // 9: A B C D F G
};

// Паттерн для прочерка "--" (активирован только сегмент G, индекс 6)
constexpr uint8_t kDashPattern = 0b01000000;

// Пины основного светофора и потенциометра
constexpr uint8_t POT_PIN = 5;
constexpr uint8_t RED_PIN = 15;
constexpr uint8_t YELLOW_PIN = 7;
constexpr uint8_t GREEN_PIN = 6;

// Интервалы аппаратных таймеров (в микросекундах)
constexpr uint32_t kTrafficTimerIntervalUs = 250000; // 250 мс тики светофора
constexpr uint32_t kScanTimerIntervalUs = 4000;      // 4 мс динамическая индикация дисплея
constexpr uint32_t kPotTimerIntervalUs = 20000;      // 20 мс опрос потенциометра

// Перечисление состояний светофора
enum class LightColor
{
    RED,
    RED_YELLOW,
    GREEN,
    GREEN_BLINK
};

// Глобальные переменные таймеров
hw_timer_t *gTrafficTimer = nullptr;
hw_timer_t *gScanTimer = nullptr;
hw_timer_t *gPotTimer = nullptr;
portMUX_TYPE gTimerMux = portMUX_INITIALIZER_UNLOCKED;

volatile uint32_t gPendingTrafficTicks = 0;
volatile uint32_t gPendingPotSamples = 0;

// Общие переменные для дисплея, доступные из прерывания
volatile uint8_t gDisplayValue = 0;
volatile bool gShowDashes = false;
volatile uint8_t gActiveDigit = 0;

// ISR для дисплея: выводит либо цифры, либо прочерки (--), если gShowDashes == true
void IRAM_ATTR onScanTimer()
{
    for (uint8_t i = 0; i < sizeof(kDigitEnablePins); ++i)
    {
        digitalWrite(kDigitEnablePins[i], kDigitEnableOffLevel);
    }

    for (uint8_t i = 0; i < sizeof(kSegmentPins); ++i)
    {
        digitalWrite(kSegmentPins[i], kSegmentOffLevel);
    }

    uint8_t pattern = 0;
    if (gShowDashes)
    {
        pattern = kDashPattern;
    }
    else
    {
        uint8_t clampedValue = gDisplayValue > 99 ? 99 : gDisplayValue;
        uint8_t digitVal = (gActiveDigit == 0) ? (clampedValue / 10) : (clampedValue % 10);
        pattern = kDigitPatterns[digitVal % 10];
    }

    for (size_t segmentIndex = 0; segmentIndex < sizeof(kSegmentPins); ++segmentIndex)
    {
        bool segmentOn = (pattern & (1U << segmentIndex)) != 0;
        digitalWrite(kSegmentPins[segmentIndex], segmentOn ? kSegmentOnLevel : kSegmentOffLevel);
    }

    digitalWrite(kDigitEnablePins[gActiveDigit], kDigitEnableOnLevel);
    gActiveDigit = (gActiveDigit + 1) % sizeof(kDigitEnablePins);
}

void IRAM_ATTR onTrafficTimer()
{
    portENTER_CRITICAL_ISR(&gTimerMux);
    ++gPendingTrafficTicks;
    portEXIT_CRITICAL_ISR(&gTimerMux);
}

void IRAM_ATTR onPotTimer()
{
    portENTER_CRITICAL_ISR(&gTimerMux);
    ++gPendingPotSamples;
    portEXIT_CRITICAL_ISR(&gTimerMux);
}

uint32_t consumePendingCount(volatile uint32_t &pendingCounter)
{
    portENTER_CRITICAL(&gTimerMux);
    const uint32_t count = pendingCounter;
    pendingCounter = 0;
    portEXIT_CRITICAL(&gTimerMux);
    return count;
}

// Класс для потенциометра
class Potentiometer
{
    uint8_t pin;
    int brightness = 255;

    static constexpr uint8_t WINDOW_SIZE = 10;
    int readings[WINDOW_SIZE] = {0};
    uint8_t readIndex = 0;
    long total = 0;

public:
    explicit Potentiometer(uint8_t p) : pin(p)
    {
        analogReadResolution(12);
        int initialVal = analogRead(pin);
        int mappedVal = map(initialVal, 0, 4095, 0, 255);
        for (uint8_t i = 0; i < WINDOW_SIZE; i++)
        {
            readings[i] = mappedVal;
            total += mappedVal;
        }
        brightness = mappedVal;
    }

    void update()
    {
        int potRaw = analogRead(pin);
        int newMapped = map(potRaw, 0, 4095, 0, 255);

        total -= readings[readIndex];
        readings[readIndex] = newMapped;
        total += readings[readIndex];
        readIndex = (readIndex + 1) % WINDOW_SIZE;

        int average = total / WINDOW_SIZE;

        if (abs(average - brightness) > 1)
        {
            brightness = average;
        }
    }

    int getBrightness() const
    {
        return brightness;
    }
};

// Класс светофора
class TrafficLightSystem
{
    struct LightPinConfig
    {
        uint8_t pin;
        ledc_channel_t channel;
    };

    LightPinConfig red;
    LightPinConfig yellow;
    LightPinConfig green;

    LightColor currentState = LightColor::GREEN;
    int ticksRemaining = 40;
    bool greenBlinkOn = true;

    struct StateInfo
    {
        int ticks;
    };

    StateInfo states[4] = {
        {60}, // RED (15 сек)
        {8},  // RED_YELLOW (2 сек)
        {40}, // GREEN (10 сек)
        {10}  // GREEN_BLINK (2.5 сек)
    };

    int lastBrightness = -1;
    LightColor lastAppliedState = static_cast<LightColor>(-1);
    bool lastBlinkOn = !greenBlinkOn;

    void setLedBrightness(const LightPinConfig &led, int brightness)
    {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, led.channel, brightness);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, led.channel);
    }

    void transitionNextState()
    {
        switch (currentState)
        {
        case LightColor::RED:
            currentState = LightColor::RED_YELLOW;
            break;
        case LightColor::RED_YELLOW:
            currentState = LightColor::GREEN;
            break;
        case LightColor::GREEN:
            currentState = LightColor::GREEN_BLINK;
            greenBlinkOn = true;
            break;
        case LightColor::GREEN_BLINK:
            currentState = LightColor::RED;
            break;
        }

        int stateIndex = static_cast<int>(currentState);
        ticksRemaining = states[stateIndex].ticks;

        Serial.printf("Switched state: %d, ticks left: %d\n", stateIndex, ticksRemaining);
    }

public:
    TrafficLightSystem(uint8_t r, uint8_t y, uint8_t g)
        : red{r, LEDC_CHANNEL_0},
          yellow{y, LEDC_CHANNEL_1},
          green{g, LEDC_CHANNEL_2} {}

    void begin()
    {
        ledc_timer_config_t ledc_timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_8_BIT,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = 5000,
        };
        ledc_timer_config(&ledc_timer);

        auto configChannel = [](const LightPinConfig &cfg)
        {
            ledc_channel_config_t channel_config = {
                .gpio_num = cfg.pin,
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .channel = cfg.channel,
                .intr_type = LEDC_INTR_DISABLE,
                .timer_sel = LEDC_TIMER_0,
                .duty = 0,
                .hpoint = 0};
            ledc_channel_config(&channel_config);
        };

        configChannel(red);
        configChannel(yellow);
        configChannel(green);
    }

    void processTick()
    {
        if (currentState == LightColor::GREEN_BLINK)
        {
            greenBlinkOn = !greenBlinkOn;
        }

        if (ticksRemaining > 0)
        {
            ticksRemaining--;
        }

        if (ticksRemaining <= 0)
        {
            transitionNextState();
        }
    }

    void updateOutputs(int brightness)
    {
        if (currentState != lastAppliedState || brightness != lastBrightness || greenBlinkOn != lastBlinkOn)
        {
            switch (currentState)
            {
            case LightColor::RED:
                setLedBrightness(red, brightness);
                setLedBrightness(yellow, 0);
                setLedBrightness(green, 0);
                break;

            case LightColor::RED_YELLOW:
                setLedBrightness(red, brightness);
                setLedBrightness(yellow, brightness);
                setLedBrightness(green, 0);
                break;

            case LightColor::GREEN:
                setLedBrightness(red, 0);
                setLedBrightness(yellow, 0);
                setLedBrightness(green, brightness);
                break;

            case LightColor::GREEN_BLINK:
                setLedBrightness(red, 0);
                setLedBrightness(yellow, 0);
                setLedBrightness(green, greenBlinkOn ? brightness : 0);
                break;
            }

            lastAppliedState = currentState;
            lastBrightness = brightness;
            lastBlinkOn = greenBlinkOn;
        }
    }

    uint8_t getRemainingSeconds() const
    {
        return static_cast<uint8_t>((ticksRemaining + 3) / 4);
    }

    // Возвращает true для состояний, где нужно выводить прочерки (RED_YELLOW и GREEN_BLINK)
    bool shouldShowDashes() const
    {
        return (currentState == LightColor::RED_YELLOW || currentState == LightColor::GREEN_BLINK);
    }
};

// Глобальные объекты
Potentiometer pot(POT_PIN);
TrafficLightSystem trafficLight(RED_PIN, YELLOW_PIN, GREEN_PIN);

void setup()
{
    Serial.begin(115200);
    delay(1000);

    for (uint8_t pin : kSegmentPins)
    {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, kSegmentOffLevel);
    }
    for (uint8_t pin : kDigitEnablePins)
    {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, kDigitEnableOffLevel);
    }

    trafficLight.begin();

    gTrafficTimer = timerBegin(0, 80, true);
    timerAttachInterrupt(gTrafficTimer, &onTrafficTimer, true);
    timerAlarmWrite(gTrafficTimer, kTrafficTimerIntervalUs, true);
    timerAlarmEnable(gTrafficTimer);

    gScanTimer = timerBegin(1, 80, true);
    timerAttachInterrupt(gScanTimer, &onScanTimer, true);
    timerAlarmWrite(gScanTimer, kScanTimerIntervalUs, true);
    timerAlarmEnable(gScanTimer);

    gPotTimer = timerBegin(2, 80, true);
    timerAttachInterrupt(gPotTimer, &onPotTimer, true);
    timerAlarmWrite(gPotTimer, kPotTimerIntervalUs, true);
    timerAlarmEnable(gPotTimer);

    Serial.printf("\nSetup successful\n");
}

void loop()
{
    if (consumePendingCount(gPendingPotSamples) > 0)
    {
        pot.update();
    }

    const uint32_t trafficTicks = consumePendingCount(gPendingTrafficTicks);
    for (uint32_t i = 0; i < trafficTicks; ++i)
    {
        trafficLight.processTick();
    }

    int currentBrightness = pot.getBrightness();
    trafficLight.updateOutputs(currentBrightness);

    // Передаем данные на дисплей
    gDisplayValue = trafficLight.getRemainingSeconds();
    gShowDashes = trafficLight.shouldShowDashes();
}
