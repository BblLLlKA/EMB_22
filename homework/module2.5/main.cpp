#include <Arduino.h>

struct Config
{
  static constexpr uint8_t relayPin = 16;

  static constexpr uint8_t relayOn = HIGH;
  static constexpr uint8_t relayOff = LOW;

  static constexpr uint32_t periodSeconds = 20;
  static constexpr uint32_t onTimeSeconds = 5;

  // For the operating mode (1 hour of work / 15-minute break)
  // static constexpr uint32_t periodSeconds = 3600; // 1 hour
  // static constexpr uint32_t onTimeSeconds = 900;  // 15 minute

  static constexpr uint8_t timerId = 0;
  static constexpr uint16_t timerPrescaler = 80;
  static constexpr uint64_t timerAlarmUs = 1000000ULL;
};

hw_timer_t *timer = nullptr;

volatile uint32_t secondsElapsed = 0;
volatile bool stateChanged = false;
volatile bool fanOn = false;

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR onTimer()
{
  portENTER_CRITICAL_ISR(&timerMux);

  secondsElapsed++;

  if (secondsElapsed >= Config::periodSeconds)
  {
    secondsElapsed = 0;
  }

  bool newState = (secondsElapsed < Config::onTimeSeconds);

  if (newState != fanOn)
  {
    fanOn = newState;
    stateChanged = true;
  }

  portEXIT_CRITICAL_ISR(&timerMux);
}

void setup()
{
  pinMode(Config::relayPin, OUTPUT);
  digitalWrite(Config::relayPin, Config::relayOff);

  timer = timerBegin(Config::timerId, Config::timerPrescaler, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, Config::timerAlarmUs, true);
  timerAlarmEnable(timer);
}

void loop()
{
  bool update = false;
  bool currentState = false;

  portENTER_CRITICAL(&timerMux);

  if (stateChanged)
  {
    stateChanged = false;
    update = true;
    currentState = fanOn;
  }

  portEXIT_CRITICAL(&timerMux);

  if (update)
  {
    digitalWrite(
        Config::relayPin,
        currentState ? Config::relayOn : Config::relayOff);
  }
}
