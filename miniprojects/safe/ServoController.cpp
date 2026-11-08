#include "ServoController.h"
#include "Config.h"
#include <Arduino.h>

namespace
{
struct Movement
{
    uint8_t angle;
    uint16_t duration;
};

constexpr Movement errorMoves[] = {
    {SERVO_ERROR_ANGLE1, SERVO_ERROR_MOVE1_MS},
    {SERVO_LOCK_ANGLE, SERVO_ERROR_MOVE2_MS},
    {SERVO_ERROR_ANGLE2, SERVO_ERROR_MOVE3_MS},
    {SERVO_LOCK_ANGLE, SERVO_ERROR_MOVE4_MS},
};
}

ServoController* ServoController::owner = nullptr;
bool ServoController::timerAllocated = false;

ServoController::ServoController()
    : state(State::Idle), initialized(false), currentAngle(SERVO_LOCK_ANGLE),
      startAngle(SERVO_LOCK_ANGLE), targetAngle(SERVO_LOCK_ANGLE), errorStep(0),
      moveStartedAt(0), lastStepAt(0), moveDuration(0), holdStartedAt(0)
{
}

ServoController::~ServoController()
{
    detach();
}

bool ServoController::begin()
{
    if (initialized)
        return true;

    if (!digitalPinCanOutput(SERVO_PIN) ||
        SERVO_MIN_ANGLE > SERVO_LOCK_ANGLE || SERVO_LOCK_ANGLE > SERVO_MAX_ANGLE ||
        SERVO_UNLOCK_ANGLE < SERVO_MIN_ANGLE || SERVO_UNLOCK_ANGLE > SERVO_MAX_ANGLE ||
        SERVO_ERROR_ANGLE1 < SERVO_MIN_ANGLE || SERVO_ERROR_ANGLE1 > SERVO_MAX_ANGLE ||
        SERVO_ERROR_ANGLE2 < SERVO_MIN_ANGLE || SERVO_ERROR_ANGLE2 > SERVO_MAX_ANGLE ||
        SERVO_MAX_ANGLE > 180 || SERVO_STEP_INTERVAL_MS == 0 ||
        SERVO_MIN_PULSE_US < 500 || SERVO_MAX_PULSE_US > 2500 ||
        SERVO_MIN_PULSE_US >= SERVO_MAX_PULSE_US || SERVO_FREQUENCY_HZ == 0 ||
        static_cast<uint32_t>(SERVO_MAX_PULSE_US) * SERVO_FREQUENCY_HZ >= 1000000UL ||
        SERVO_LEDC_TIMER > 3 ||
        SERVO_ERROR_MOVE1_MS == 0 || SERVO_ERROR_MOVE2_MS == 0 ||
        SERVO_ERROR_MOVE3_MS == 0 || SERVO_ERROR_MOVE4_MS == 0)
    {
        log_e("Invalid servo configuration");
        return false;
    }

    if (owner != nullptr)
    {
        log_e("Servo timer is already in use");
        return false;
    }
    if (!timerAllocated)
    {
        // ESP32Servo resets global timer bookkeeping in explicit allocation mode.
        if (ESP32PWM::PWMCount > 0)
        {
            log_e("Servo timer reservation must precede other ESP32Servo allocations");
            return false;
        }
        ESP32PWM::allocateTimer(SERVO_LEDC_TIMER);
        timerAllocated = true;
    }
    // ESP32Servo 0.13.0 otherwise spins forever when no suitable channel exists.
    const uint8_t firstChannel = SERVO_LEDC_TIMER * 2;
    if (ESP32PWM::timerCount[SERVO_LEDC_TIMER] >= 4 ||
        (ESP32PWM::timerFreqSet[SERVO_LEDC_TIMER] != -1 &&
         ESP32PWM::timerFreqSet[SERVO_LEDC_TIMER] != SERVO_FREQUENCY_HZ) ||
        (ESP32PWM::ChannelUsed[firstChannel] != nullptr &&
         ESP32PWM::ChannelUsed[firstChannel + 1] != nullptr))
    {
        log_e("No available servo PWM channel");
        return false;
    }
    servo.setPeriodHertz(SERVO_FREQUENCY_HZ);
    if (!servo.attach(SERVO_PIN, SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US) || !servo.attached())
    {
        log_e("Cannot attach servo");
        return false;
    }
    initialized = true;
    owner = this;
    currentAngle = SERVO_LOCK_ANGLE;
    servo.write(currentAngle);
    return true;
}

void ServoController::beginMove(uint8_t target, uint32_t duration, State nextState)
{
    startAngle = currentAngle;
    targetAngle = target;
    moveDuration = duration;
    moveStartedAt = millis();
    lastStepAt = moveStartedAt;
    state = nextState;
}

void ServoController::unlock()
{
    if (!initialized || isMoving())
    {
        log_e("Servo not initialized or busy");
        return;
    }
    const uint32_t distance = currentAngle > SERVO_UNLOCK_ANGLE ?
        currentAngle - SERVO_UNLOCK_ANGLE : SERVO_UNLOCK_ANGLE - currentAngle;
    beginMove(SERVO_UNLOCK_ANGLE, distance * SERVO_STEP_INTERVAL_MS, State::Unlocking);
}

void ServoController::lock()
{
    if (!initialized)
    {
        log_e("Servo not initialized");
        return;
    }
    const uint32_t distance = currentAngle > SERVO_LOCK_ANGLE ?
        currentAngle - SERVO_LOCK_ANGLE : SERVO_LOCK_ANGLE - currentAngle;
    beginMove(SERVO_LOCK_ANGLE, distance * SERVO_STEP_INTERVAL_MS, State::Locking);
}

void ServoController::playErrorMovement()
{
    if (!initialized || isMoving())
    {
        log_e("Servo not initialized or busy");
        return;
    }
    errorStep = 0;
    beginMove(errorMoves[0].angle, errorMoves[0].duration, State::Error);
}

void ServoController::finishMove(uint32_t now)
{
    switch (state)
    {
    case State::Unlocking:
        state = State::Waiting;
        holdStartedAt = now;
        break;
    case State::Error:
        if (++errorStep < sizeof(errorMoves) / sizeof(errorMoves[0]))
            beginMove(errorMoves[errorStep].angle, errorMoves[errorStep].duration, State::Error);
        else
            state = State::Idle;
        break;
    case State::Locking:
        state = State::Idle;
        break;
    default:
        break;
    }
}

void ServoController::update()
{
    if (!initialized || state == State::Idle)
        return;

    const uint32_t now = millis();
    if (state == State::Waiting)
    {
        if (now - holdStartedAt >= SERVO_UNLOCK_HOLD_MS)
            lock();
        return;
    }

    const uint32_t elapsed = now - moveStartedAt;
    if (elapsed < moveDuration && now - lastStepAt < SERVO_STEP_INTERVAL_MS)
        return;

    lastStepAt = now;
    uint8_t nextAngle = targetAngle;
    if (elapsed < moveDuration)
    {
        const int32_t difference = static_cast<int32_t>(targetAngle) - startAngle;
        nextAngle = static_cast<uint8_t>(startAngle +
            difference * static_cast<int32_t>(elapsed) / static_cast<int32_t>(moveDuration));
    }
    if (nextAngle != currentAngle)
    {
        currentAngle = nextAngle;
        servo.write(currentAngle);
    }
    if (elapsed >= moveDuration)
        finishMove(now);
}

void ServoController::stop()
{
    if (!initialized)
        return;
    state = State::Idle;
    currentAngle = SERVO_LOCK_ANGLE;
    servo.write(currentAngle);
}

bool ServoController::isMoving() const
{
    return state != State::Idle;
}

void ServoController::detach()
{
    if (!initialized)
        return;
    stop();
    servo.detach();
    initialized = false;
    owner = nullptr;
}
