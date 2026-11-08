#pragma once

#include <stdint.h>
#include <ESP32Servo.h>

/** Drives the servo through nonblocking lock and feedback sequences. */
class ServoController
{
public:
    ServoController();
    ~ServoController();

    ServoController(const ServoController&) = delete;
    ServoController& operator=(const ServoController&) = delete;

    bool begin();
    void unlock();
    void lock();
    void playErrorMovement();
    void update();
    void stop();
    bool isMoving() const;
    void detach();

private:
    enum class State : uint8_t { Idle, Unlocking, Waiting, Locking, Error };

    void beginMove(uint8_t target, uint32_t duration, State nextState);
    void finishMove(uint32_t now);

    static ServoController* owner;
    static bool timerAllocated;
    Servo servo;
    State state;
    bool initialized;
    uint8_t currentAngle;
    uint8_t startAngle;
    uint8_t targetAngle;
    uint8_t errorStep;
    uint32_t moveStartedAt;
    uint32_t lastStepAt;
    uint32_t moveDuration;
    uint32_t holdStartedAt;
};
