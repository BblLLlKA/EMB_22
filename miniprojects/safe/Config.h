#pragma once

#include <Arduino.h>

constexpr uint32_t SERIAL_BAUD_RATE = 115200;

constexpr uint8_t SEGMENT_PINS[] = {
    10, // A
    11, // B
    12, // C
    13, // D
    16, // E
    17, // F
    18, // G
};

constexpr uint8_t DIGIT_ENABLE_PINS[] = {
    8, // rightmost digit (position 0)
    9,
};

constexpr uint8_t DISPLAY_DIGIT_COUNT = sizeof(DIGIT_ENABLE_PINS) / sizeof(DIGIT_ENABLE_PINS[0]);
constexpr uint8_t ENCODER_CLK_PIN = 2;
constexpr uint8_t ENCODER_DT_PIN = 3;
constexpr uint8_t ENCODER_SW_PIN = 4;
// RotaryEncoder latch modes: FOUR3 = 1, FOUR0 = 2, TWO03 = 3.
constexpr uint8_t ENCODER_LATCH_MODE = 1;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t ACTIVE_DIGIT_BLINK_MS = 500;

constexpr uint8_t BUZZER_PIN = 5;
constexpr uint8_t BUZZER_LEDC_CHANNEL = 0;
constexpr uint16_t BUZZER_INITIAL_FREQUENCY_HZ = 1000;
// Core 2.x ledcWriteTone() uses a fixed 10-bit resolution.
constexpr uint8_t BUZZER_PWM_RESOLUTION_BITS = 10;
constexpr uint16_t BUZZER_ERROR_FREQUENCY_HZ = 1000;
constexpr uint16_t BUZZER_ERROR_FIRST_MS = 150;
constexpr uint16_t BUZZER_ERROR_PAUSE_MS = 100;
constexpr uint16_t BUZZER_ERROR_SECOND_MS = 300;
constexpr uint16_t BUZZER_SUCCESS_FIRST_HZ = 1500;
constexpr uint16_t BUZZER_SUCCESS_FIRST_MS = 100;
constexpr uint16_t BUZZER_SUCCESS_PAUSE_MS = 50;
constexpr uint16_t BUZZER_SUCCESS_SECOND_HZ = 2000;
constexpr uint16_t BUZZER_SUCCESS_SECOND_MS = 150;

constexpr uint8_t SERVO_PIN = 7;
constexpr uint16_t SERVO_MIN_PULSE_US = 500;
constexpr uint16_t SERVO_MAX_PULSE_US = 2400;
constexpr uint8_t SERVO_MIN_ANGLE = 0;
constexpr uint8_t SERVO_MAX_ANGLE = 180;
constexpr uint8_t SERVO_LOCK_ANGLE = 0;
constexpr uint8_t SERVO_UNLOCK_ANGLE = 90;
constexpr uint16_t SERVO_FREQUENCY_HZ = 50;
constexpr uint16_t SERVO_STEP_INTERVAL_MS = 10;
constexpr uint16_t SERVO_UNLOCK_HOLD_MS = 5000;
constexpr uint8_t SERVO_ERROR_ANGLE1 = 15;
constexpr uint8_t SERVO_ERROR_ANGLE2 = 25;
constexpr uint16_t SERVO_ERROR_MOVE1_MS = 200;
constexpr uint16_t SERVO_ERROR_MOVE2_MS = 200;
constexpr uint16_t SERVO_ERROR_MOVE3_MS = 250;
constexpr uint16_t SERVO_ERROR_MOVE4_MS = 250;
constexpr uint8_t SERVO_LEDC_TIMER = 1;

constexpr uint8_t SEGMENT_ON_LEVEL = LOW;
constexpr uint8_t SEGMENT_OFF_LEVEL = HIGH;

constexpr uint8_t DIGIT_ENABLE_ON_LEVEL = HIGH;
constexpr uint8_t DIGIT_ENABLE_OFF_LEVEL = LOW;

// Arduino-ESP32 2.x: reserve this hardware timer for the display.
constexpr uint8_t DISPLAY_TIMER_INDEX = 0;
// The core 2.x APB clock is 80 MHz; divider 80 gives 1 MHz timer ticks.
constexpr uint16_t DISPLAY_TIMER_DIVIDER = 80;
constexpr uint32_t DISPLAY_REFRESH_HZ = 1000;

constexpr uint8_t DIGIT_PATTERNS[] = {
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
