# ESP32 Smart Safe

## Overview

https://youtube.com/shorts/j2IhM-2YLP8?feature=share

This educational ESP32-S3 project combines a multiplexed seven-segment
display, rotary encoder, passive buzzer and servo-operated lock. The first
complete encoder entry sets the PIN; subsequent entries check it. Sounds
and servo movement are nonblocking, and the display is refreshed by a
hardware timer.

**This is not a security product.** The PIN is kept in RAM, is displayed
while entered, and is lost on reset. There is no secure storage, attempt
limit, tamper detection, power-loss protection or physical lock feedback.
Do not use it to protect valuables or for safety-critical applications.

## Hardware and wiring

- ESP32-S3; PlatformIO targets 4D Systems GEN4-ESP32 S3 R8N16.
- Two-digit multiplexed LED display and appropriately rated segment
  resistors and digit driver circuitry.
- Quadrature rotary encoder with normally open pushbutton, wired to ground.
- Passive buzzer with a suitable driver if its current exceeds GPIO limits.
- Servo and appropriate external power supply.

The actual default signal mapping in `Config.h` is:

| Component | Signal | ESP32 GPIO |
|---|---|---:|
| Display | Segment A | 10 |
| Display | Segment B | 11 |
| Display | Segment C | 12 |
| Display | Segment D | 13 |
| Display | Segment E | 16 |
| Display | Segment F | 17 |
| Display | Segment G | 18 |
| Display | Right digit enable, position 0 | 8 |
| Display | Left digit enable, position 1 | 9 |
| Encoder | CLK | 2 |
| Encoder | DT | 3 |
| Encoder | SW | 4 |
| Buzzer | Signal | 5 |
| Servo | PWM signal | 7 |

Segments are active-low and digit enables active-high in this configuration.
Verify that these levels match the actual driver circuit. The encoder inputs
use internal pull-ups; SW is active-low. GPIO validity and duplicate assignments
are checked at startup, but the firmware cannot tell whether a valid GPIO is
reserved by the board, flash, PSRAM, USB, strapping circuits or other hardware.
Check the board schematic before wiring. Do not connect display segments or
motors directly if their current exceeds ESP32 pin ratings.

Use an external supply capable of the servo's peak/stall current, and connect
its ground to ESP32 GND. Calibrate pulse widths, angles and linkage with the
lock disconnected first. A servo may move or emit a transient pulse at startup;
the controller cannot verify its physical position.

## Dependencies and installation

- Arduino-ESP32 **2.x** (tested by compilation with 2.0.17). The display timer
  and LEDC interfaces are not compatible with Core 3.x.
- Matthias Hertel `RotaryEncoder` 1.6.0 (PlatformIO range `^1.5.3`).
- Kevin Harrington/John Bennett `ESP32Servo` **0.13.0**.

For PlatformIO, use the `safe` environment; the project's default environment
is `module25`:

```powershell
platformio run -e safe
platformio run -e safe -t upload
platformio device monitor -e safe
```

In Arduino IDE, install Arduino-ESP32 Core 2.0.17 and both libraries through
Library Manager. Create a sketch named `safe` with all the `.h` and `.cpp`
files from this directory, or rename `main.cpp` to `safe.ino` in the sketch
copy. Select the matching ESP32-S3 board, select its serial port and upload.
For USB-UART monitoring, disable **USB CDC On Boot**; the PlatformIO `safe`
environment does this explicitly. Monitor at 115200 baud. Board-specific flash
and PSRAM settings must match the hardware.

## Project structure

```text
lessons/
├── platformio.ini
├── src/safe/
│   ├── Config.h
│   ├── main.cpp
│   ├── SegmentDisplay.h / SegmentDisplay.cpp
│   ├── SafePinCode.h / SafePinCode.cpp
│   ├── Buzzer.h / Buzzer.cpp
│   ├── ServoController.h / ServoController.cpp
│   └── README.md
```

## Configuration

Edit `Config.h` for pin assignments, polarity, encoder latch mode, debounce,
active-digit blink period, display timer and refresh rate, sound frequencies
and durations, servo angles, pulse widths, movement timings and LEDC resources.
Constants use `UPPER_SNAKE_CASE`. For example:

```cpp
constexpr uint8_t ENCODER_CLK_PIN = 2;
constexpr uint32_t ACTIVE_DIGIT_BLINK_MS = 500;
constexpr uint8_t SERVO_UNLOCK_ANGLE = 90;
```

Digit pins are ordered right to left. The PIN width is the number of entries
in `DIGIT_ENABLE_PINS`; add actual distinct digit GPIOs for more digits.
The driver supports 1–10 digits, and a 1000 Hz scan gives each digit
1000 / digit count refreshes per second. Table bits A–G encode digits 0–9;
the display has no brightness or decimal-point support. Its timer divider
and refresh rate must satisfy the compile-time checks.

The servo uses LEDC timer 1 and the buzzer uses channel 0, whose timer is 0.
ESP32-S3 LEDC channels 0/1, 2/3, 4/5 and 6/7 share timers. If changing
these settings, reserve separate timers for servo and buzzer and all other
PWM users. The display's hardware timer 0 is a separate peripheral. Change
`monitor_speed` in `platformio.ini` if `SERIAL_BAUD_RATE` is changed.
PlatformIO sets `CORE_DEBUG_LEVEL=1` to expose core error logs; select the
equivalent core debug level in Arduino IDE when troubleshooting.

## Operation and modules

### PIN input

`SafePinCode` polls the Hertel encoder and the debounced button in `update()`.
Use `ENCODER_LATCH_MODE` 1 (FOUR3), 2 (FOUR0) or 3 (TWO03) to match the
encoder. Swap CLK and DT if direction is reversed. Poll frequently: without
interrupts, fast turns can be missed.

After startup the display shows `0` followed by dashes. The active digit
blinks, and turning changes it cyclically from 0 to 9. A debounced press
confirms a digit; holding the button generates only one press. Confirmed
digits remain visible and the next digit starts at zero. The first complete
entry sets the PIN and triggers `wasPinSet()`; it does not unlock. Following
entries trigger `wasPinEntered()` once and `isPinCorrect()` holds the latest
result. Events may coalesce if they are not consumed before another attempt.

Leading zeroes are part of the fixed-width comparison. `setPin(42)` means
`0042` on a four-digit display; never write `0042` as a C++ literal because
it is octal. PIN storage is `uint32_t`; ten-digit entries above 4294967295
are rejected. `resetInput()` resets the partial input, not the saved PIN or
the last verification result. `hasPin()` distinguishes an unset PIN.
`setPin()` is an explicit programmatic override.

### Display

`SegmentDisplay` owns the ESP32 hardware timer and a RAM-resident buffer.
Its 1000 Hz ISR turns off the previous digit, applies precomputed segment
GPIO masks and enables the next one. ESP32 FreeRTOS critical sections protect
the shared buffer, scanning state and GPIO operations across the ISR and both
cores. The driver creates no FreeRTOS task; simply disabling interrupts on one
core would not protect against access from the other. The ISR does not print,
allocate or delay. `displayNumber()`
uses leading zeroes and displays dashes on overflow. `displayDigit()` accepts
digits, -1 (dash) and -2 (blank); position 0 is rightmost.

Only one display instance can own the timer. `begin()` starts scanning,
`clear()` blanks the buffer and outputs, and `stop()` blanks outputs while
retaining the buffer. Lifecycle methods must be serialized; initialize and
destroy on the same core. Arduino Core 2.x does not report every timer or
interrupt-attachment failure through its timer wrapper. Reserve the timer
and inspect logs; IRAM placement alone does not guarantee uninterrupted
scanning during flash operations.

### Buzzer

`Buzzer` executes one sequence at a time; a new request while busy is ignored
and logged. `stop()` interrupts it. Default success is 1500 Hz for 100 ms,
50 ms silence, then 2000 Hz for 150 ms. Error is 1000 Hz for 150 ms,
100 ms silence, then 1000 Hz for 300 ms. `beep(0, duration)` is a timed
pause. Late `update()` calls extend steps. The PWM channel is configured with
zero duty before attachment; `begin()` checks timer and channel setup errors.
Arduino Core 2.x `ledcWriteTone()` fixes the PWM resolution at 10 bits, so
`BUZZER_PWM_RESOLUTION_BITS` must remain 10.

### Servo

`ServoController` advances through opening, waiting, closing, and error
movement states. The default opening goes from 0 to 90 degrees, waits 5000 ms
after commanding 90, then returns to 0. Error movement targets 15, 0, 25 and
0 degrees with durations 200, 200, 250 and 250 ms. Angle interpolation uses
`millis()`; delayed updates can cause larger physical steps and longer
sequences. No physical position feedback exists.

Busy `unlock()` and `playErrorMovement()` requests are ignored and logged.
`lock()` overrides the active sequence and starts a smooth return. `stop()`
immediately commands the closed angle; `detach()` commands that angle and
disables PWM without waiting for physical motion. Detached servos may not
hold position. Only one controller can be initialized.

ESP32Servo 0.13.0 uses global LEDC allocation state and does not detect
channels allocated outside its library. The project reserves a separate
servo timer, checks the library's channel availability, and must initialize
its reservation before other ESP32Servo users. Timer exhaustion or external
PWM ownership cannot be reliably detected in all cases. Do not share the
reserved timer. PWM attachment can generate a transient initial pulse.

### Main loop

`setup()` validates configured GPIOs and resource assignments and checks
module initialization. `loop()` calls `safe.update()`, `buzzer.update()`
and `servoController.update()` and consumes PIN events. Display refresh
continues in the timer ISR. No application `delay()` is used. Public methods
should be called from one task, not from an ISR.

## Troubleshooting

- **No serial output:** choose the USB-UART COM port and baud rate, upload
  environment `safe`, disable USB CDC On Boot and reset. Reset erases the PIN.
- **Wrong digits or ghosting:** check segment order, polarity, digit order,
  resistors, transistors and wiring.
- **Encoder skips or moves backwards:** call `update()` often, select the
  correct latch mode, and swap CLK/DT for reversed direction.
- **Servo jitters or ESP32 resets:** check servo supply capacity, shared ground,
  LEDC timer ownership and mechanical limits.
- **Silent buzzer:** check passive buzzer wiring, driver and channel ownership.
- **Initialization fails:** inspect serial output and core error logs; check
  duplicate pins, LEDC timer assignments, memory and peripheral availability.

## Known limitations and future improvements

Hardware behavior has to be verified on the assembled device, including
electrical levels, flicker, timing, power and simultaneous peripherals.
Diagnostic `log_e` messages depend on the Arduino core log level. The system
does not provide persistence, lockout, encrypted credentials or sensor
feedback. Future work could add an authenticated PIN enrollment and secure
storage design, attempt throttling, lock-position sensing and hardware
integration tests. These features require a separate security and safety
design, not just a firmware flag.
